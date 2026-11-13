/*
 * Performance HUD - ReShade add-on
 *
 * Shows CPU/GPU usage, power draw, temperatures, memory usage and FPS stats
 * in a single column on ReShade's always-visible on-screen display (OSD):
 * every metric is rendered on its own line.
 *
 * Data sources:
 *  - CPU usage:      GetSystemTimes (kernel/user time deltas)
 *  - RAM usage:      GlobalMemoryStatusEx (physical memory in use)
 *  - GPU usage:      NVML (NVIDIA) with PDH \GPU Engine(*)\Utilization
 *                    Percentage fallback (what Task Manager uses, Windows 10+)
 *  - GPU power/temp: NVML only (vendor SDK access is required for AMD/Intel)
 *  - VRAM usage:     NVML \c nvmlDeviceGetMemoryInfo with a PDH
 *                    \GPU Adapter Memory(*)\Dedicated Usage fallback
 *                    (only the used amount is shown, not the total)
 *  - FPS/percent low: derived from swapchain present times within a rolling
 *                    10 second window, so the statistics continuously reset
 *
 * Copyright (C) 2026 reshade-better-color-and-texture contributors
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>

#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

static constexpr int kMaxGpus = 16;
static constexpr uint32_t kMaxCores = 256;

static void log_info(const char *fmt, ...)
{
	char message[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	reshade::log::message(reshade::log::level::info, message);
}

static void log_warning(const char *fmt, ...)
{
	char message[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	reshade::log::message(reshade::log::level::warning, message);
}

// --------------------------------------------------------------- statistics

struct GpuSample
{
	char name[96];
	float usage;
	float power;
	int   temp;
	float vram_used_gb;       // dedicated video memory used
	float vram_total_gb;
	bool  usage_valid;
	bool  power_valid;
	bool  temp_valid;
	bool  vram_valid;
};

struct StatsSnapshot
{
	bool  cpu_usage_valid;
	float cpu_usage;          // percent 0-100
	bool  core_usage_valid;
	int   core_count;
	float core_usage[kMaxCores]; // every logical core, percent 0-100
	bool  ram_usage_valid;
	float ram_used_gb;        // physical memory in use
	float ram_free_gb;        // physical memory still available
	bool  battery_valid;
	int   battery_percent;    // 0-100
	bool  gpu_fallback_valid; // PDH GPU Engine counters are in use
	float gpu_fallback_usage; // percent 0-100
	bool  gpu_fallback_vram_valid;
	float gpu_fallback_vram_usage; // gigabytes
	bool  has_nvidia;
	int   gpu_count;
	GpuSample gpus[kMaxGpus];
};

static StatsSnapshot g_snapshot = {};
static SRWLOCK g_snapshot_lock = SRWLOCK_INIT;

// --------------------------------------------------------------- settings

static bool g_show_cpu_usage = true;
static bool g_show_gpu_usage = true;
static bool g_show_temp = true;
static bool g_show_power = true;
static bool g_show_vram = true;
static bool g_show_ram = true;
static bool    g_show_fps = true;
static bool    g_smooth_motion = false;       // double FPS stats (e.g. NVIDIA Smooth Motion)
static bool g_show_battery = true;
static bool g_show_core = false;           // usage of every logical core, colored by load

static constexpr float g_hud_pos_x = 8.0f;   // distance from the left screen edge, in pixels
static float g_hud_pos_y = 0.0f;          // vertical offset from the top screen edge, in pixels (positive or negative)
static float g_hud_scale = 1.0f;          // font scale of the OSD text (ReShade's global font size provides the base resolution)
static float g_hud_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // #FFFFFFFF
static bool g_use_threshold_colors = true; // color usage lines by load level
static std::atomic<uint32_t> g_interval_ms { 1000 };

// --------------------------------------------------------------- NVML

namespace nvml
{
	constexpr int NVML_SUCCESS = 0;
	constexpr int NVML_TEMPERATURE_GPU = 0;

	struct Utilization { uint32_t gpu; uint32_t memory; };
	struct MemoryInfo { uint64_t total_bytes; uint64_t free_bytes; uint64_t used_bytes; };

	using Init_t =            int (*)();
	using Shutdown_t =        int (*)();
	using DeviceGetCount_t =  int (*)(uint32_t *);
	using DeviceGetHandle_t = int (*)(uint32_t, void **);
	using DeviceGetName_t =   int (*)(void *, char *, uint32_t);
	using DeviceGetUtil_t =   int (*)(void *, Utilization *);
	using DeviceGetPower_t =  int (*)(void *, uint32_t *);
	using DeviceGetTemp_t =   int (*)(void *, int, uint32_t *);
	using DeviceGetEnergy_t = int (*)(void *, uint64_t *);
	using DeviceGetMemInfo_t = int (*)(void *, MemoryInfo *);

	static HMODULE module = nullptr;
	static Init_t init = nullptr;
	static Shutdown_t shutdown = nullptr;
	static DeviceGetCount_t device_get_count = nullptr;
	static DeviceGetHandle_t device_get_handle = nullptr;
	static DeviceGetName_t device_get_name = nullptr;
	static DeviceGetUtil_t device_get_util = nullptr;
	static DeviceGetPower_t device_get_power = nullptr;
	static DeviceGetTemp_t device_get_temp = nullptr;
	static DeviceGetEnergy_t device_get_energy = nullptr;
	static DeviceGetMemInfo_t device_get_mem_info = nullptr;
}

static void *g_nvml_devices[kMaxGpus] = { nullptr };
static int g_nvml_device_count = 0;

// Previous energy counters, used to derive power from an energy delta on GPUs
// that do not report instantaneous power.
static ULARGE_INTEGER g_prev_energy_tick[kMaxGpus];
static uint64_t g_prev_energy[kMaxGpus] = { 0 };
static bool g_prev_energy_valid[kMaxGpus] = { false };

static void release_nvml()
{
	if (nvml::module != nullptr && nvml::shutdown != nullptr)
	{
		// The library itself stays loaded for the remainder of the process
		// lifetime; release_nvml is only ever called during teardown.
		nvml::shutdown();
	}
	g_nvml_device_count = 0;
}

static void load_nvml()
{
	HMODULE module = LoadLibraryW(L"nvml.dll");
	if (module == nullptr)
	{
		// nvml.dll ships with the NVIDIA display driver, but is not always on
		// the DLL search path. Probe the driver store as a fallback.
		WIN32_FIND_DATAW find_data;
		HANDLE find_handle = FindFirstFileW(L"C:\\Windows\\System32\\DriverStore\\FileRepository\\nv*.inf_amd64_*\\nvml.dll", &find_data);
		if (find_handle != INVALID_HANDLE_VALUE)
		{
			do
			{
				wchar_t path[MAX_PATH];
				swprintf_s(path, L"C:\\Windows\\System32\\DriverStore\\FileRepository\\%ls\\nvml.dll", find_data.cFileName);
				module = LoadLibraryW(path);
			} while (module == nullptr && FindNextFileW(find_handle, &find_data));
			FindClose(find_handle);
		}
	}
	if (module == nullptr)
	{
		log_info("Performance HUD: no NVML library found.");
		return;
	}

#define NVML_PROC(name, variable, signature) \
	variable = reinterpret_cast<signature>(reinterpret_cast<void *>(GetProcAddress(module, name)))
	NVML_PROC("nvmlInit_v1", nvml::init, nvml::Init_t);
	if (nvml::init == nullptr)
		NVML_PROC("nvmlInit", nvml::init, nvml::Init_t);
	NVML_PROC("nvmlShutdown", nvml::shutdown, nvml::Shutdown_t);
	NVML_PROC("nvmlDeviceGetCount_v2", nvml::device_get_count, nvml::DeviceGetCount_t);
	NVML_PROC("nvmlDeviceGetHandleByIndex_v2", nvml::device_get_handle, nvml::DeviceGetHandle_t);
	NVML_PROC("nvmlDeviceGetName", nvml::device_get_name, nvml::DeviceGetName_t);
	NVML_PROC("nvmlDeviceGetUtilizationRates", nvml::device_get_util, nvml::DeviceGetUtil_t);
	NVML_PROC("nvmlDeviceGetPowerUsage", nvml::device_get_power, nvml::DeviceGetPower_t);
	NVML_PROC("nvmlDeviceGetTemperature", nvml::device_get_temp, nvml::DeviceGetTemp_t);
	NVML_PROC("nvmlDeviceGetTotalEnergyConsumption", nvml::device_get_energy, nvml::DeviceGetEnergy_t);
	NVML_PROC("nvmlDeviceGetMemoryInfo", nvml::device_get_mem_info, nvml::DeviceGetMemInfo_t);
#undef NVML_PROC

	if (nvml::init == nullptr || nvml::device_get_count == nullptr || nvml::device_get_handle == nullptr ||
		nvml::device_get_util == nullptr)
	{
		FreeLibrary(module);
		return;
	}
	if (nvml::init() != nvml::NVML_SUCCESS)
	{
		FreeLibrary(module);
		return;
	}

	nvml::module = module;

	uint32_t count = 0;
	if (nvml::device_get_count(&count) != nvml::NVML_SUCCESS)
	{
		release_nvml();
		return;
	}
	if (count > kMaxGpus)
		count = kMaxGpus;

	for (uint32_t i = 0; i < count && g_nvml_device_count < kMaxGpus; ++i)
	{
		void *device = nullptr;
		if (nvml::device_get_handle(i, &device) == nvml::NVML_SUCCESS && device != nullptr)
			g_nvml_devices[g_nvml_device_count++] = device;
	}

	log_info("Performance HUD: initialized NVML with %d GPU(s).", g_nvml_device_count);
}

// --------------------------------------------------------------- PDH

static PDH_HQUERY g_pdh_query = nullptr;
static PDH_HCOUNTER g_pdh_gpu_counter = nullptr;
static bool g_pdh_gpu_available = false;
static PDH_HCOUNTER g_pdh_gpu_mem_counter = nullptr;
static bool g_pdh_gpu_mem_available = false;

// Grow-capable buffer for \GPU Engine wildcard formatted counter values
static PDH_FMT_COUNTERVALUE_ITEM_W *g_pdh_items = nullptr;
static DWORD g_pdh_items_size = 0;

static void release_pdh()
{
	if (g_pdh_query != nullptr)
	{
		PdhCloseQuery(g_pdh_query);
		g_pdh_query = nullptr;
	}
	g_pdh_gpu_counter = nullptr;
	g_pdh_gpu_mem_counter = nullptr;
	g_pdh_gpu_available = false;
	g_pdh_gpu_mem_available = false;
	if (g_pdh_items != nullptr)
	{
		HeapFree(GetProcessHeap(), 0, g_pdh_items);
		g_pdh_items = nullptr;
		g_pdh_items_size = 0;
	}
}

static void load_pdh()
{
	if (PdhOpenQueryW(nullptr, 0, &g_pdh_query) != ERROR_SUCCESS)
	{
		g_pdh_query = nullptr;
		return;
	}

	// GPU engine utilization, used as fallback while NVML is not available
	g_pdh_gpu_available =
		(PdhAddEnglishCounterW(g_pdh_query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_pdh_gpu_counter) == ERROR_SUCCESS);

	// Dedicated GPU adapter memory, used as VRAM fallback while NVML is not
	// available
	g_pdh_gpu_mem_available =
		(PdhAddEnglishCounterW(g_pdh_query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &g_pdh_gpu_mem_counter) == ERROR_SUCCESS);

	if (g_pdh_gpu_available)
		log_info("Performance HUD: GPU engine counters are available.");
	if (g_pdh_gpu_mem_available)
		log_info("Performance HUD: GPU adapter memory counters are available.");
}

// Reports the battery charge level. BatteryLifePercent is 255 when unknown
// and the system has no battery at all, in which case nothing is shown.
static bool sample_battery(int *out_percent)
{
	SYSTEM_POWER_STATUS status;
	if (!GetSystemPowerStatus(&status) || status.BatteryLifePercent == 255 || status.BatteryLifePercent > 100)
		return false;

	*out_percent = static_cast<int>(status.BatteryLifePercent);
	return true;
}

// Reports the physical RAM in use system-wide, via GlobalMemoryStatusEx.
static bool sample_ram_usage(float *out_gb, float *out_free_gb)
{
	MEMORYSTATUSEX status = { sizeof(MEMORYSTATUSEX) };
	if (!GlobalMemoryStatusEx(&status) || status.ullTotalPhys == 0)
		return false;

	constexpr float bytes_to_gib = 1.0f / (1024.0f * 1024.0f * 1024.0f);
	*out_gb = static_cast<float>(status.ullTotalPhys - status.ullAvailPhys) * bytes_to_gib;
	*out_free_gb = static_cast<float>(status.ullAvailPhys) * bytes_to_gib;
	return true;
}

// Finds the dedicated adapter memory usage of the busiest GPU instance, which
// is how Task Manager reports "Dedicated GPU memory". Values are in bytes.
static bool sample_gpu_adapter_memory(uint64_t *out_bytes)
{
	if (!g_pdh_gpu_mem_available)
		return false;

	// First call with a null buffer determines the required buffer size
	DWORD buffer_size = 0;
	DWORD item_count = 0;
	PDH_STATUS status = PdhGetFormattedCounterArrayW(g_pdh_gpu_mem_counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, nullptr);
	if (status != PDH_MORE_DATA || buffer_size == 0)
		return false;

	if (buffer_size > g_pdh_items_size)
	{
		void *const new_items = HeapReAlloc(GetProcessHeap(), 0, g_pdh_items, buffer_size);
		if (new_items == nullptr)
			return false;
		g_pdh_items = static_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(new_items);
		g_pdh_items_size = buffer_size;
	}

	status = PdhGetFormattedCounterArrayW(g_pdh_gpu_mem_counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, g_pdh_items);
	if (status != ERROR_SUCCESS)
		return false;

	double max_bytes = 0.0;
	bool any_valid = false;
	for (DWORD i = 0; i < item_count; ++i)
	{
		if (g_pdh_items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA)
			continue;
		any_valid = true;
		if (g_pdh_items[i].FmtValue.doubleValue > max_bytes)
			max_bytes = g_pdh_items[i].FmtValue.doubleValue;
	}

	*out_bytes = static_cast<uint64_t>(max_bytes);
	return any_valid;
}

// Finds the highest summed utilization across GPU engine instances, grouped
// per physical adapter, which is also how Task Manager reports "GPU" load.
static bool sample_gpu_engine_utilization(float *out_utilization)
{
	*out_utilization = 0.0f;

	// First call with a null buffer determines the required buffer size
	DWORD buffer_size = 0;
	DWORD item_count = 0;
	PDH_STATUS status = PdhGetFormattedCounterArrayW(g_pdh_gpu_counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, nullptr);
	if (status != PDH_MORE_DATA || buffer_size == 0)
		return false;

	if (buffer_size > g_pdh_items_size)
	{
		void *const new_items = HeapReAlloc(GetProcessHeap(), 0, g_pdh_items, buffer_size);
		if (new_items == nullptr)
			return false;
		g_pdh_items = static_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(new_items);
		g_pdh_items_size = buffer_size;
	}

	status = PdhGetFormattedCounterArrayW(g_pdh_gpu_counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, g_pdh_items);
	if (status != ERROR_SUCCESS)
		return false;

	// Instances look like 'pid_1234_luid_0x00000000_0x0000E381_phys_0_eng_10_engtype_Compute_1'.
	// Group by everything up to the first '_eng_', i.e. per LUID/physical adapter.
	struct Group { wchar_t key[96]; float utilization; };
	Group groups[32];
	int group_count = 0;

	float max_utilization = 0.0f;
	for (DWORD i = 0; i < item_count; ++i)
	{
		const wchar_t *const instance = g_pdh_items[i].szName;
		if (instance == nullptr || g_pdh_items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA)
			continue;

		const wchar_t *const luid = wcsstr(instance, L"luid_");
		if (luid == nullptr)
			continue;
		const wchar_t *const eng = wcsstr(luid, L"_eng_");
		if (eng == nullptr)
			continue;

		const size_t key_length = static_cast<size_t>(eng - luid);
		if (key_length == 0 || key_length >= 96)
			continue;

		int group_index = -1;
		for (int j = 0; j < group_count; ++j)
		{
			if (wcsncmp(groups[j].key, luid, key_length) == 0 && groups[j].key[key_length] == L'\0')
			{
				group_index = j;
				break;
			}
		}
		if (group_index < 0)
		{
			if (group_count >= 32)
				continue;
			group_index = group_count++;
			wcsncpy_s(groups[group_index].key, luid, key_length);
			groups[group_index].utilization = 0.0f;
		}

		groups[group_index].utilization += static_cast<float>(g_pdh_items[i].FmtValue.doubleValue);
		if (groups[group_index].utilization > max_utilization)
			max_utilization = groups[group_index].utilization;
	}

	*out_utilization = (max_utilization > 100.0f ? 100.0f : max_utilization);
	return (group_count > 0);
}

// --------------------------------------------------------------- CPU usage

static ULARGE_INTEGER g_prev_idle_time = {};
static ULARGE_INTEGER g_prev_time_value = {};
static bool g_prev_time_valid = false;

static bool sample_cpu_usage(float *out_usage)
{
	FILETIME idle_time, kernel_time, user_time;
	if (!GetSystemTimes(&idle_time, &kernel_time, &user_time))
	{
		g_prev_time_valid = false;
		return false;
	}

	ULARGE_INTEGER idle, kernel, user;
	idle.LowPart = idle_time.dwLowDateTime;
	idle.HighPart = idle_time.dwHighDateTime;
	kernel.LowPart = kernel_time.dwLowDateTime;
	kernel.HighPart = kernel_time.dwHighDateTime;
	user.LowPart = user_time.dwLowDateTime;
	user.HighPart = user_time.dwHighDateTime;

	// Kernel time already includes idle time, so busy time = (kernel + user) - idle
	const uint64_t idle_now = idle.QuadPart;
	const uint64_t time_now = kernel.QuadPart + user.QuadPart;

	float usage = 0.0f;
	bool valid = g_prev_time_valid;
	if (valid)
	{
		const double idle_delta = static_cast<double>(idle_now) - static_cast<double>(g_prev_idle_time.QuadPart);
		const double time_delta = static_cast<double>(time_now) - static_cast<double>(g_prev_time_value.QuadPart);
		if (time_delta > 0.0)
			usage = static_cast<float>(100.0 - 100.0 * idle_delta / time_delta);
		else
			valid = false;
	}

	g_prev_idle_time.QuadPart = idle_now;
	g_prev_time_value.QuadPart = time_now;
	g_prev_time_valid = true;

	*out_usage = usage;
	return valid;
}

// Per-logical-core usage via NtQuerySystemInformation(SystemProcessorPerformanceInformation).
// Returns the busiest core. Kernel time includes idle time, as in GetSystemTimes.
namespace ntdll
{
	struct ProcessorPerformanceInfo
	{
		LARGE_INTEGER IdleTime;
		LARGE_INTEGER KernelTime;
		LARGE_INTEGER UserTime;
		LARGE_INTEGER DpcTime;
		LARGE_INTEGER InterruptTime;
		ULONG InterruptCount;
	};
	using Query_t = LONG (NTAPI *)(ULONG, PVOID, ULONG, PULONG);
	static Query_t query = nullptr;
	static constexpr ULONG kSystemProcessorPerformanceInformation = 8;
}

static uint64_t g_prev_core_idle[kMaxCores] = { 0 };
static uint64_t g_prev_core_total[kMaxCores] = { 0 };
static bool g_prev_core_valid = false;

static void load_ntdll()
{
	HMODULE module = GetModuleHandleW(L"ntdll.dll");
	if (module != nullptr)
		ntdll::query = reinterpret_cast<ntdll::Query_t>(reinterpret_cast<void *>(
			GetProcAddress(module, "NtQuerySystemInformation")));
}

// Fills out_usage[0..*out_count) with the usage of every logical core.
static bool sample_core_usage(float *out_usage, int *out_count)
{
	*out_count = 0;
	if (ntdll::query == nullptr)
		return false;

	const DWORD core_count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	if (core_count == 0 || core_count > kMaxCores)
		return false;

	ntdll::ProcessorPerformanceInfo info[kMaxCores];
	ULONG returned = 0;
	const LONG status = ntdll::query(ntdll::kSystemProcessorPerformanceInformation, info,
		static_cast<ULONG>(sizeof(info[0]) * core_count), &returned);
	if (status < 0)
	{
		g_prev_core_valid = false;
		return false;
	}

	const uint32_t count = std::min<uint32_t>(returned / sizeof(info[0]), kMaxCores);
	const bool valid = g_prev_core_valid;
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint64_t idle_now = static_cast<uint64_t>(info[i].IdleTime.QuadPart);
		const uint64_t total_now = static_cast<uint64_t>(info[i].KernelTime.QuadPart) +
			static_cast<uint64_t>(info[i].UserTime.QuadPart);

		if (valid && total_now > g_prev_core_total[i])
		{
			const double idle_delta = static_cast<double>(idle_now) - static_cast<double>(g_prev_core_idle[i]);
			const double total_delta = static_cast<double>(total_now) - static_cast<double>(g_prev_core_total[i]);
			out_usage[i] = static_cast<float>(std::clamp(100.0 - 100.0 * idle_delta / total_delta, 0.0, 100.0));
		}
		else
		{
			out_usage[i] = 0.0f;
		}

		g_prev_core_idle[i] = idle_now;
		g_prev_core_total[i] = total_now;
	}
	g_prev_core_valid = true;

	*out_count = static_cast<int>(count);
	return valid;
}

// --------------------------------------------------------------- sampling

static void update_snapshot()
{
	StatsSnapshot next;
	memset(&next, 0, sizeof(next));

	// CPU usage via time deltas
	next.cpu_usage_valid = sample_cpu_usage(&next.cpu_usage);
	// Per-core usage is only sampled while "Show core usage" is enabled
	if (g_show_core)
	{
		next.core_usage_valid = sample_core_usage(next.core_usage, &next.core_count);
	}
	else
	{
		g_prev_core_valid = false;
	}

	// Collect performance counter values once per interval, before reading them
	const bool use_gpu_fallback = (g_nvml_device_count == 0 && g_pdh_gpu_available);
	const bool use_gpu_memory_fallback = (g_nvml_device_count == 0 && g_pdh_gpu_mem_available);
	if (use_gpu_fallback || use_gpu_memory_fallback)
		PdhCollectQueryData(g_pdh_query);

	next.ram_usage_valid = sample_ram_usage(&next.ram_used_gb, &next.ram_free_gb);
	next.battery_valid = sample_battery(&next.battery_percent);

	if (g_nvml_device_count > 0)
	{
		next.has_nvidia = true;
		next.gpu_count = g_nvml_device_count;

		const ULONGLONG now = GetTickCount64();
		for (int i = 0; i < g_nvml_device_count; ++i)
		{
			void *device = g_nvml_devices[i];
			GpuSample &gpu = next.gpus[i];

			if (nvml::device_get_name != nullptr)
				nvml::device_get_name(device, gpu.name, sizeof(gpu.name));

			nvml::Utilization utilization = { 0, 0 };
			if (nvml::device_get_util(device, &utilization) == nvml::NVML_SUCCESS)
			{
				gpu.usage = static_cast<float>(utilization.gpu);
				gpu.usage_valid = true;
			}

			uint32_t power_milliwatt = 0;
			if (nvml::device_get_power != nullptr &&
				nvml::device_get_power(device, &power_milliwatt) == nvml::NVML_SUCCESS)
			{
				gpu.power = static_cast<float>(power_milliwatt) / 1000.0f;
				gpu.power_valid = true;
				g_prev_energy_valid[i] = false;
			}
			else if (nvml::device_get_energy != nullptr)
			{
				// Derive power from an energy delta: [mJ] / [ms] == [W]
				uint64_t energy_millijoule = 0;
				if (nvml::device_get_energy(device, &energy_millijoule) == nvml::NVML_SUCCESS)
				{
					if (g_prev_energy_valid[i] && now > g_prev_energy_tick[i].QuadPart && energy_millijoule > g_prev_energy[i])
					{
						const uint64_t tick_delta = now - g_prev_energy_tick[i].QuadPart;
						const uint64_t energy_delta = energy_millijoule - g_prev_energy[i];
						gpu.power = static_cast<float>(energy_delta) / static_cast<float>(tick_delta);
						gpu.power_valid = true;
					}
					g_prev_energy_tick[i].QuadPart = now;
					g_prev_energy[i] = energy_millijoule;
					g_prev_energy_valid[i] = true;
				}
			}

			uint32_t temperature = 0;
			if (nvml::device_get_temp != nullptr &&
				nvml::device_get_temp(device, nvml::NVML_TEMPERATURE_GPU, &temperature) == nvml::NVML_SUCCESS)
			{
				gpu.temp = static_cast<int>(temperature);
				gpu.temp_valid = true;
			}

			nvml::MemoryInfo mem = { 0, 0, 0 };
			if (nvml::device_get_mem_info != nullptr &&
				nvml::device_get_mem_info(device, &mem) == nvml::NVML_SUCCESS && mem.total_bytes > 0)
			{
				// NVML reports memory in bytes
				constexpr float bytes_to_gib = 1.0f / (1024.0f * 1024.0f * 1024.0f);
				gpu.vram_used_gb = static_cast<float>(mem.used_bytes) * bytes_to_gib;
				gpu.vram_total_gb = static_cast<float>(mem.total_bytes) * bytes_to_gib;
				gpu.vram_valid = true;
			}
		}
	}
	else if (use_gpu_fallback || use_gpu_memory_fallback)
	{
		if (use_gpu_fallback)
			next.gpu_fallback_valid = sample_gpu_engine_utilization(&next.gpu_fallback_usage);

		uint64_t vram_bytes = 0;
		if (use_gpu_memory_fallback && sample_gpu_adapter_memory(&vram_bytes) && vram_bytes > 0)
		{
			constexpr float bytes_to_gib = 1.0f / (1024.0f * 1024.0f * 1024.0f);
			next.gpu_fallback_vram_usage = static_cast<float>(vram_bytes) * bytes_to_gib;
			next.gpu_fallback_vram_valid = true;
		}
	}

	// Publish
	AcquireSRWLockExclusive(&g_snapshot_lock);
	g_snapshot = next;
	ReleaseSRWLockExclusive(&g_snapshot_lock);
}

static std::atomic<bool> g_sampler_once_flag { false };
static HANDLE g_sampler_stop_event = nullptr;
static HANDLE g_sampler_thread = nullptr;

static DWORD WINAPI sampler_thread(LPVOID)
{
	for (;;)
	{
		const DWORD wait_result = WaitForSingleObject(g_sampler_stop_event, static_cast<DWORD>(g_interval_ms.load()));
		if (wait_result == WAIT_OBJECT_0)
			break;
		update_snapshot();
	}
	return 0;
}

static void start_sampler_thread()
{
	bool expected = false;
	if (g_sampler_once_flag.compare_exchange_strong(expected, true))
	{
		g_sampler_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		g_sampler_thread = CreateThread(nullptr, 0, sampler_thread, nullptr, 0, nullptr);
		if (g_sampler_thread == nullptr)
		{
			if (g_sampler_stop_event != nullptr)
				CloseHandle(g_sampler_stop_event);
			g_sampler_stop_event = nullptr;
			g_sampler_once_flag = false;
		}
	}
}

// --------------------------------------------------------------- frame times

// Rolling history of the most recent frame durations with a presentation
// timestamp per frame. Statistics are computed from the frames inside a
// rolling window, so they reset continuously.
static constexpr uint32_t kFrameHistory = 8192;           // ~10 s of frames even at ~800 FPS
static constexpr ULONGLONG kFpsWindowMs = 10000;          // 10 second rolling window

struct FrameRing
{
	SRWLOCK lock = SRWLOCK_INIT;
	uint32_t head = 0;              // total number of frames written
	float ms[kFrameHistory] = { 0.0f };
	ULONGLONG tick[kFrameHistory] = { 0 }; // presentation time in ms
};
static FrameRing g_frames;

struct FpsStats
{
	bool  valid;
	float fps;                      // average FPS across the history window
	float low1;                     // 1% low FPS
	float low01;                    // 0.1% low FPS
};
static FpsStats g_fps_stats = {};
static SRWLOCK g_fps_stats_lock = SRWLOCK_INIT;
static ULARGE_INTEGER g_last_stats_tick = {};
static float g_frames_copy[kFrameHistory];

// Average duration of the slowest frames in a sorted (ascending) frame time
// buffer.
static double slowest_average_ms(const float *sorted_ms, uint32_t count, uint32_t slowest)
{
	double sum = 0.0;
	for (uint32_t i = count - slowest; i < count; ++i)
		sum += sorted_ms[i];
	return sum / static_cast<double>(slowest);
}

static void record_frame_time(float frame_ms)
{
	AcquireSRWLockExclusive(&g_frames.lock);
	g_frames.ms[g_frames.head % kFrameHistory] = frame_ms;
	g_frames.tick[g_frames.head % kFrameHistory] = GetTickCount64();
	++g_frames.head;
	ReleaseSRWLockExclusive(&g_frames.lock);
}

// Recomputes FPS and percent-low statistics from the frame history within a
// rolling 10 second window, so they reset every 10 seconds, at most twice
// per second.
static void update_fps_stats()
{
	const ULONGLONG now = GetTickCount64();
	if (g_last_stats_tick.QuadPart != 0 && now - g_last_stats_tick.QuadPart < 500)
		return;
	g_last_stats_tick.QuadPart = now;

	const ULONGLONG window_start = now - kFpsWindowMs;

	uint32_t count = 0;
	{
		AcquireSRWLockShared(&g_frames.lock);
		const uint32_t head = g_frames.head;
		const uint32_t begin = (head > kFrameHistory) ? (head - kFrameHistory) : 0;
		for (uint32_t i = begin; i < head; ++i)
		{
			if (g_frames.tick[i % kFrameHistory] >= window_start)
				g_frames_copy[count++] = g_frames.ms[i % kFrameHistory];
		}
		ReleaseSRWLockShared(&g_frames.lock);
	}

	FpsStats stats = {};
	stats.valid = (count >= 32); // not enough history yet to be meaningful
	if (stats.valid)
	{
		double time_sum_ms = 0.0;
		for (uint32_t i = 0; i < count; ++i)
			time_sum_ms += g_frames_copy[i];
		stats.fps = static_cast<float>(1000.0 * count / time_sum_ms);

		// Percent lows: 1% / 0.1% low FPS is the frame rate equivalent of the
		// average of the slowest 1% / 0.1% of frames in the history window.
		std::sort(g_frames_copy, g_frames_copy + count);

		const uint32_t count_1 = count / 100 > 0 ? count / 100 : 1;
		const uint32_t count_01 = count / 1000 > 0 ? count / 1000 : 1;
		stats.low1 = static_cast<float>(1000.0 / slowest_average_ms(g_frames_copy, count, count_1));
		stats.low01 = static_cast<float>(1000.0 / slowest_average_ms(g_frames_copy, count, count_01));
	}

	AcquireSRWLockExclusive(&g_fps_stats_lock);
	g_fps_stats = stats;
	ReleaseSRWLockExclusive(&g_fps_stats_lock);
}

// --------------------------------------------------------------- OSD

static const ImVec4 color_for_percent(float percent)
{
	if (percent >= 90.0f)
		return ImVec4(1.00f, 0.36f, 0.36f, 1.0f);
	if (percent >= 60.0f)
		return ImVec4(1.00f, 0.78f, 0.25f, 1.0f);
	return ImVec4(0.63f, 0.93f, 0.65f, 1.0f);
}

static const ImVec4 base_color()
{
	return ImVec4(g_hud_color[0], g_hud_color[1], g_hud_color[2], g_hud_color[3]);
}

static const ImVec4 line_color(float percent)
{
	return g_use_threshold_colors ? color_for_percent(percent) : base_color();
}

// Threshold levels shared by the FPS, memory and temperature lines
static constexpr int kLevelNone = 0;
static constexpr int kLevelWarning = 1;
static constexpr int kLevelCritical = 2;

// FPS counters: 60+ white, below 60 yellow, below 30 red
static int fps_level(float fps)
{
	if (fps < 30.0f)
		return kLevelCritical;
	if (fps < 60.0f)
		return kLevelWarning;
	return kLevelNone;
}

// VRAM/RAM lines color by the amount still available: under 1 GB yellow,
// under 0.5 GB red
static int mem_available_level(float free_gb)
{
	if (free_gb < 0.5f)
		return kLevelCritical;
	if (free_gb < 1.0f)
		return kLevelWarning;
	return kLevelNone;
}

// Battery line: below 50% yellow, below 30% red
static int battery_level(int percent)
{
	if (percent < 30)
		return kLevelCritical;
	if (percent < 50)
		return kLevelWarning;
	return kLevelNone;
}

// CPU/GPU usage lines: 60+ % yellow, 90+ % red (matches color_for_percent)
static int usage_level(float percent)
{
	if (percent >= 90.0f)
		return kLevelCritical;
	if (percent >= 60.0f)
		return kLevelWarning;
	return kLevelNone;
}

// Temperature lines: above 70 C yellow, above 80 C red
static int temperature_level(float celsius)
{
	if (celsius > 80.0f)
		return kLevelCritical;
	if (celsius > 70.0f)
		return kLevelWarning;
	return kLevelNone;
}

static const ImVec4 level_color(int level)
{
	if (g_use_threshold_colors)
	{
		if (level >= kLevelCritical)
			return ImVec4(1.00f, 0.36f, 0.36f, 1.0f);
		if (level >= kLevelWarning)
			return ImVec4(1.00f, 0.78f, 0.25f, 1.0f);
	}
	return base_color();
}

// Single-column layout: every line is "value label", e.g. "60% cpu".
static void draw_metric_line(const ImVec4 &color, const char *value, const char *label)
{
	ImGui::TextColored(color, "%s %s", value, label);
}

static void draw_metric_placeholder(const char *label)
{
	static const ImVec4 disabled_color = { 0.55f, 0.55f, 0.55f, 1.0f };
	draw_metric_line(disabled_color, "...", label);
}


// Usage line carries the threshold coloring; other metrics are separate lines
// below it in the same column.
static void draw_usage_metric(const char *value, const char *label, float percent)
{
	draw_metric_line(line_color(percent), value, label);
}

static void draw_cpu_lines(const StatsSnapshot &snapshot)
{
	// With "Show performance issues only", the usage line auto-shows when the
	// CPU load reaches a warning level, even if "Show CPU usage" is off.
	const bool cpu_usage_warning = (g_use_threshold_colors && snapshot.cpu_usage_valid &&
		usage_level(snapshot.cpu_usage) > kLevelNone);
	if (g_show_cpu_usage || cpu_usage_warning)
	{
		if (snapshot.cpu_usage_valid)
		{
			char value[16];
			snprintf(value, sizeof(value), "%.0f%%", snapshot.cpu_usage);
			draw_usage_metric(value, "cpu", snapshot.cpu_usage);
		}
		else if (g_show_cpu_usage)
		{
			draw_metric_placeholder("cpu");
		}
	}

	// One line per logical core, colored by load. Only when enabled.
	if (g_show_core)
	{
		if (snapshot.core_usage_valid)
		{
			for (int i = 0; i < snapshot.core_count; ++i)
			{
				char value[16];
				char label[16];
				snprintf(value, sizeof(value), "%.0f%%", snapshot.core_usage[i]);
				snprintf(label, sizeof(label), "core%d", i);
				draw_usage_metric(value, label, snapshot.core_usage[i]);
			}
		}
		else
		{
			draw_metric_placeholder("core");
		}
	}

	const bool show_ram_line = g_show_ram || (g_use_threshold_colors && snapshot.ram_usage_valid &&
		mem_available_level(snapshot.ram_free_gb) > kLevelNone);
	if (show_ram_line)
	{
		if (snapshot.ram_usage_valid)
		{
			char value[16];
			snprintf(value, sizeof(value), "%.1f", snapshot.ram_used_gb);
			draw_metric_line(level_color(mem_available_level(snapshot.ram_free_gb)), value, "ram");
		}
		else
		{
			draw_metric_placeholder("ram");
		}
	}

	const bool show_battery_line = g_show_battery || (g_use_threshold_colors && snapshot.battery_valid &&
		battery_level(snapshot.battery_percent) > kLevelNone);
	if (show_battery_line && snapshot.battery_valid)
	{
		char value[16];
		snprintf(value, sizeof(value), "%d%%", snapshot.battery_percent);
		draw_metric_line(level_color(battery_level(snapshot.battery_percent)), value, "bat");
	}
}

static FpsStats fps_stats_snapshot()
{
	AcquireSRWLockShared(&g_fps_stats_lock);
	const FpsStats stats = g_fps_stats;
	ReleaseSRWLockShared(&g_fps_stats_lock);
	return stats;
}

static void draw_fps_lines(reshade::api::effect_runtime *)
{
	const FpsStats stats = fps_stats_snapshot();

	if (!stats.valid)
		return;

	// Smooth Motion / frame generation presents interpolated frames, so the
	// presented rate is roughly double the rendered rate.
	const float scale = g_smooth_motion ? 2.0f : 1.0f;
	const float fps = stats.fps * scale;
	const float low1 = stats.low1 * scale;
	const float low01 = stats.low01 * scale;

	const bool any_warning = fps_level(fps) > kLevelNone || fps_level(low1) > kLevelNone ||
		fps_level(low01) > kLevelNone;
	if (!g_show_fps && !(g_use_threshold_colors && any_warning))
		return;

	char value[16];
	snprintf(value, sizeof(value), "%.0f", fps);
	draw_metric_line(level_color(fps_level(fps)), value, "fps");
	snprintf(value, sizeof(value), "%.0f", low1);
	draw_metric_line(level_color(fps_level(low1)), value, "1%");
	snprintf(value, sizeof(value), "%.0f", low01);
	draw_metric_line(level_color(fps_level(low01)), value, "0.1%");
}

static void draw_gpu_lines(const StatsSnapshot &snapshot)
{
	if (snapshot.has_nvidia)
	{
		for (int i = 0; i < snapshot.gpu_count && i < kMaxGpus; ++i)
		{
			const GpuSample &gpu = snapshot.gpus[i];
			const bool show_usage = (g_show_gpu_usage && gpu.usage_valid);

			// VRAM colors by the memory that is still available, so a known
			// total is required; the PDH fallback only reports the used amount
			const float vram_free_gb = (gpu.vram_valid && gpu.vram_total_gb > 0.0f) ?
				gpu.vram_total_gb - gpu.vram_used_gb : 0.0f;
			const int vram_level = (gpu.vram_valid && gpu.vram_total_gb > 0.0f) ?
				mem_available_level(vram_free_gb) : kLevelNone;
			const bool show_vram = (g_show_vram || (g_use_threshold_colors && vram_level > kLevelNone)) &&
				gpu.vram_valid && gpu.vram_used_gb >= 0.05f;

			const int temp_level = (gpu.temp_valid) ?
				temperature_level(static_cast<float>(gpu.temp)) : kLevelNone;
			const bool show_power = (g_show_power && gpu.power_valid);
			const bool show_temp = (g_show_temp || (g_use_threshold_colors && temp_level > kLevelNone)) && gpu.temp_valid;

			char label[12];
			if (snapshot.gpu_count > 1)
				snprintf(label, sizeof(label), "gpu%d", i);
			else
				snprintf(label, sizeof(label), "gpu");

			// GPU usage is always shown in the base color, regardless of load
			if (show_usage)
			{
				char value[16];
				snprintf(value, sizeof(value), "%.0f%%", gpu.usage);
				draw_metric_line(base_color(), value, label);
			}

			if (show_vram)
			{
				char value[16];
				snprintf(value, sizeof(value), "%.1f", gpu.vram_used_gb);
				draw_metric_line(level_color(vram_level), value, "vram");
			}

			if (show_power)
			{
				char value[16];
				snprintf(value, sizeof(value), "%.0f", gpu.power);
				draw_metric_line(base_color(), value, "pow");
			}

			if (show_temp)
			{
				char value[16];
				snprintf(value, sizeof(value), "%d", gpu.temp);
				draw_metric_line(level_color(temp_level), value, "temp");
			}

			if (!show_usage && !show_vram && !show_power && !show_temp)
			{
				// Only show a placeholder while the usage line is requested
				// but no data for this GPU is available at all.
				if (g_show_gpu_usage)
					draw_metric_placeholder(label);
			}
		}
	}
	else
	{
		// PDH fallback path: usage and VRAM usage only
		bool drew_any = false;

		if (g_show_gpu_usage && snapshot.gpu_fallback_valid)
		{
			char value[16];
			snprintf(value, sizeof(value), "%.0f%%", snapshot.gpu_fallback_usage);
			draw_metric_line(base_color(), value, "gpu");
			drew_any = true;
		}

		if (g_show_vram && snapshot.gpu_fallback_vram_valid)
		{
			char value[16];
			snprintf(value, sizeof(value), "%.1f", snapshot.gpu_fallback_vram_usage);
			draw_metric_line(base_color(), value, "vram");
			drew_any = true;
		}

		if (!drew_any && g_show_gpu_usage)
			draw_metric_placeholder("gpu");
	}
}

static void draw_osd(reshade::api::effect_runtime *)
{
	AcquireSRWLockShared(&g_snapshot_lock);
	StatsSnapshot snapshot = g_snapshot;
	ReleaseSRWLockShared(&g_snapshot_lock);

	// Stats must be refreshed here, not only when the FPS lines are drawn:
	// otherwise a hidden FPS line never gets evaluated and never auto-shows.
	update_fps_stats();
	const FpsStats stats = fps_stats_snapshot();
	const float scale = g_smooth_motion ? 2.0f : 1.0f;

	// Warning-colored lines auto-show even when they are hidden by settings
	bool any_warning = false;
	if (g_use_threshold_colors)
	{
		const float fps_values[3] = { stats.fps * scale, stats.low1 * scale, stats.low01 * scale };
		if (stats.valid)
			for (const float value : fps_values)
				any_warning |= (fps_level(value) > kLevelNone);

		any_warning |= (snapshot.cpu_usage_valid && usage_level(snapshot.cpu_usage) > kLevelNone);
		any_warning |= (snapshot.ram_usage_valid && mem_available_level(snapshot.ram_free_gb) > kLevelNone);
		any_warning |= (snapshot.battery_valid && battery_level(snapshot.battery_percent) > kLevelNone);
		for (int i = 0; i < snapshot.gpu_count && i < kMaxGpus; ++i)
		{
			const GpuSample &gpu = snapshot.gpus[i];
			any_warning |= (gpu.temp_valid && temperature_level(static_cast<float>(gpu.temp)) > kLevelNone);
			any_warning |= (gpu.vram_valid && gpu.vram_total_gb > 0.0f &&
				mem_available_level(gpu.vram_total_gb - gpu.vram_used_gb) > kLevelNone);
		}
	}

	if (!g_show_cpu_usage && !g_show_gpu_usage && !g_show_temp &&
		!g_show_power && !g_show_vram && !g_show_ram && !g_show_fps && !g_show_battery && !g_show_core && !any_warning)
		return;

	// Scales the OSD text. PushFont(nullptr, FontSizeBase * scale) is the
	// Dear ImGui 1.92 replacement for the legacy ImGui::SetWindowFontScale.
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * g_hud_scale);
	// No window padding, so the HUD sits flush with the overlay's top-left edge
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	// Moves the HUD to its configured on-screen position, relative to the
	// content area of the overlay window.
	ImGui::SetCursorPos(ImVec2(g_hud_pos_x, g_hud_pos_y));

	if (g_show_fps || (g_use_threshold_colors && any_warning))
		draw_fps_lines(nullptr);

	draw_cpu_lines(snapshot);
	draw_gpu_lines(snapshot);

	ImGui::PopStyleVar();
	ImGui::PopFont();
}

// --------------------------------------------------------------- settings

static int g_interval_config = 1000;

static void save_config(reshade::api::effect_runtime *runtime)
{
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowCPU", g_show_cpu_usage);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowCore", g_show_core);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowGPU", g_show_gpu_usage);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowGPUTemp", g_show_temp);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowPower", g_show_power);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowVram", g_show_vram);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowRam", g_show_ram);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowFps", g_show_fps);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "HudScale", g_hud_scale);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ColorLinesByLoad", g_use_threshold_colors);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "RefreshIntervalMs", static_cast<int>(g_interval_ms.load()));

	char color_string[64];
	snprintf(color_string, sizeof(color_string), "%.3f,%.3f,%.3f,%.3f",
		g_hud_color[0], g_hud_color[1], g_hud_color[2], g_hud_color[3]);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "HudColor", static_cast<const char *>(color_string));
}

static void load_config()
{
	reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "ShowCPU", g_show_cpu_usage);
	reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "ShowCore", g_show_core);
	reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "ShowGPU", g_show_gpu_usage);
	reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "ShowGPUTemp", g_show_temp);
	reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "ShowPower", g_show_power);
	reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "ShowVram", g_show_vram);
	reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "ShowRam", g_show_ram);
	reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "ShowBattery", g_show_battery);
	reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "ShowFps", g_show_fps);
	reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "SmoothMotion", g_smooth_motion);
	reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "ColorLinesByLoad", g_use_threshold_colors);

	if (reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "HudScale", g_hud_scale) &&
		(g_hud_scale < 0.25f || g_hud_scale > 4.0f))
		g_hud_scale = 1.0f;

	if (reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "HudPosY", g_hud_pos_y) &&
		(g_hud_pos_y < -10000.0f || g_hud_pos_y > 10000.0f))
		g_hud_pos_y = 0.0f;

	char color_string[64];
	size_t color_length = sizeof(color_string) - 1;
	if (reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "HudColor", color_string, &color_length))
	{
		float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
		if (sscanf(color_string, "%f,%f,%f,%f", &r, &g, &b, &a) == 4)
		{
			g_hud_color[0] = (r < 0.0f ? 0.0f : r > 1.0f ? 1.0f : r);
			g_hud_color[1] = (g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g);
			g_hud_color[2] = (b < 0.0f ? 0.0f : b > 1.0f ? 1.0f : b);
			g_hud_color[3] = (a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a);
		}
	}

	if (reshade::get_config_value(nullptr, "PERFORMANCE_HUD", "RefreshIntervalMs", g_interval_config) &&
		g_interval_config >= 100 && g_interval_config <= 10000)
	{
		g_interval_ms = static_cast<uint32_t>(g_interval_config);
	}
	else
	{
		g_interval_config = 1000;
	}
}

static void draw_settings(reshade::api::effect_runtime *runtime)
{
	ImGui::SeparatorText("Metrics");
	if (ImGui::Checkbox("Show performance issues only", &g_use_threshold_colors))
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ColorLinesByLoad", g_use_threshold_colors);
	if (ImGui::Checkbox("Show CPU usage", &g_show_cpu_usage))
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowCPU", g_show_cpu_usage);
	if (ImGui::Checkbox("Show core usage", &g_show_core))
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowCore", g_show_core);
	if (ImGui::Checkbox("Show GPU usage", &g_show_gpu_usage))
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowGPU", g_show_gpu_usage);
	if (ImGui::Checkbox("Show GPU temperature", &g_show_temp))
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowGPUTemp", g_show_temp);
	if (ImGui::Checkbox("Show power draw", &g_show_power))
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowPower", g_show_power);
	if (ImGui::Checkbox("Show VRAM usage", &g_show_vram))
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowVram", g_show_vram);
	if (ImGui::Checkbox("Show RAM usage", &g_show_ram))
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowRam", g_show_ram);
	if (ImGui::Checkbox("Show battery level", &g_show_battery))
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowBattery", g_show_battery);

	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowBattery", g_show_battery);

	if (ImGui::Checkbox("Show FPS and 1%%/0.1%% low", &g_show_fps))
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "ShowFps", g_show_fps);
	if (ImGui::Checkbox("Smooth Motion (double FPS stats)", &g_smooth_motion))
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "SmoothMotion", g_smooth_motion);

	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "SmoothMotion", g_smooth_motion);


	ImGui::SeparatorText("Appearance");
	if (ImGui::SliderFloat("HUD scale##perfhud", &g_hud_scale, 0.5f, 3.0f, "%.0fx"))
	{
		// Snap to whole-number multiples (1x steps)
		g_hud_scale = std::round(g_hud_scale);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "HudScale", g_hud_scale);
	reshade::set_config_value(runtime, "PERFORMANCE_HUD", "HudPosY", g_hud_pos_y);
	}
	if (ImGui::ColorEdit4("Base text color", g_hud_color))
	{
		char color_string[64];
		snprintf(color_string, sizeof(color_string), "%.3f,%.3f,%.3f,%.3f",
			g_hud_color[0], g_hud_color[1], g_hud_color[2], g_hud_color[3]);
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "HudColor", static_cast<const char *>(color_string));
	}

	if (ImGui::InputInt("Refresh interval (ms)", &g_interval_config, 100))
	{
		// Snap to 100 ms steps and clamp to the supported range
		g_interval_config = std::clamp(g_interval_config, 100, 10000);
		g_interval_config = (g_interval_config / 100) * 100;
		g_interval_ms = static_cast<uint32_t>(g_interval_config);
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "RefreshIntervalMs", g_interval_config);
	}

	ImGui::Spacing();
	if (ImGui::SliderFloat("HUD y offset##perfhud", &g_hud_pos_y, -2000.0f, 2000.0f, "%.0f px"))
	{
		// Snap to whole pixels
		g_hud_pos_y = std::round(g_hud_pos_y);
		reshade::set_config_value(runtime, "PERFORMANCE_HUD", "HudPosY", g_hud_pos_y);
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Note: to change the HUD position, go to Settings > Overlay > \"OSD position on screen\".");

	ImGui::Spacing();
	ImGui::SeparatorText("Data sources");
	if (g_nvml_device_count > 0)
		ImGui::TextDisabled("NVML: %d NVIDIA GPU(s) found.", g_nvml_device_count);
	else
		ImGui::TextDisabled("NVML: no NVIDIA GPU found.");

	// Report live data, not just whether the counter was registered
	StatsSnapshot snapshot;
	AcquireSRWLockShared(&g_snapshot_lock);
	snapshot = g_snapshot;
	ReleaseSRWLockShared(&g_snapshot_lock);



	ImGui::TextDisabled("GPU power and temperature require NVIDIA NVML (other GPUs report usage only).");
}

// --------------------------------------------------------------- events

static void on_present(reshade::api::effect_runtime *)
{
	// Record frame durations from swapchain present cycles, for FPS and
	// percent-low statistics. Deltas above one second are discarded (device
	// recreate, process suspension, etc.).
	LARGE_INTEGER now, freq;
	if (QueryPerformanceCounter(&now) && QueryPerformanceFrequency(&freq))
	{
		static LARGE_INTEGER prev = {};
		static bool prev_valid = false;

		if (prev_valid)
		{
			const float frame_ms = static_cast<float>(
				static_cast<double>(now.QuadPart - prev.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart));
			if (frame_ms > 0.0f && frame_ms < 1000.0f)
				record_frame_time(frame_ms);
		}
		prev = now;
		prev_valid = true;
	}

	start_sampler_thread();
}

// --------------------------------------------------------------- add-on info

extern "C" __declspec(dllexport) const char *NAME = "Performance HUD";
extern "C" __declspec(dllexport) const char *AUTHOR = "reshade-better-color-and-texture";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Shows CPU/GPU usage, power draw, temperatures, VRAM/RAM usage and FPS with 1%/0.1% low stats (10 second window) in a single column on the always-visible ReShade on-screen display.";
extern "C" __declspec(dllexport) const char *WEBSITE = "https://github.com/kless/reshade-better-color-and-texture";

static void init()
{
	load_config();
	load_nvml();
	load_pdh();
	load_ntdll();

	reshade::register_overlay("OSD", &draw_osd);
	reshade::register_overlay(nullptr, &draw_settings);
	reshade::register_event<reshade::addon_event::reshade_present>(&on_present);
}

static void shutdown()
{
	reshade::unregister_event<reshade::addon_event::reshade_present>(&on_present);
	reshade::unregister_overlay(nullptr, &draw_settings);
	reshade::unregister_overlay("OSD", &draw_osd);

	// Ask the sampler thread to stop. It is deliberately not joined from within
	// DllMain (which would risk a loader lock deadlock); it exits on its own at
	// the next tick.
	if (g_sampler_stop_event != nullptr)
		SetEvent(g_sampler_stop_event);
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(instance);
		if (!reshade::register_addon(instance))
			return FALSE; // ReShade version does not support this add-on API
		init();
		break;
	case DLL_PROCESS_DETACH:
		if (reserved != nullptr) // static process termination, nothing to clean up
			break;
		shutdown();
		break;
	}
	return TRUE;
}
