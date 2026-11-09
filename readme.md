## ReShade Preset: Better Colors and Texture

This preset improves color, shadows, texture detail, and sharpness in supported games.

## Installation

### 1) Install ReShade

1. Go to the official ReShade website: https://reshade.me/
2. Download and run the installer.
3. Select your game `.exe`.
4. Choose the rendering API used by the game (DirectX 9/10/11/12, Vulkan, or OpenGL).
5. Complete installation and download the default shader package when prompted.

### 2) Download this preset as ZIP from GitHub

1. Open this repository on GitHub.
2. Click **Code** > **Download ZIP** above.
3. Extract the ZIP on your computer.

### 3) Copy preset files to the game folder

1. Open the extracted ZIP folder.
2. Copy the preset files (such as `ReShadePreset.ini` and related files/folders) to the same folder as the game executable.
3. Launch the game and open the ReShade menu (default key is usually `Home`) to load the preset.

## Performance HUD

The preset includes a ReShade add-on (`reshade-shaders/Addons/PerformanceHUD.addon64`) that displays an always-visible on-screen HUD with:

- CPU usage (and CPU package power in watts when the system provides it)
- GPU usage per adapter, with power draw and temperature on NVIDIA GPUs (via NVML)
- On non-NVIDIA GPUs, usage is shown using Windows GPU engine counters (what Task Manager uses)

Configure it under **Add-ons > Performance HUD** in the ReShade overlay (toggle lines, refresh interval). Usage turns amber above 60% and red above 90%.

Notes:

- Requires a full ReShade installation (with add-on support) that includes the `AddonPath` handling; `ReShade.ini` in this repository already sets `[ADDON] AddonPath=.\reshade-shaders\Addons`.
- Copy `reshade-shaders/Addons/PerformanceHUD.addon64` together with the other preset files next to the game executable for the HUD to load.
- To change the HUD position, go to the ReShade settings: **Settings > Overlay > "OSD position on screen"**.


## Videos

- [Watch Dogs video](https://youtu.be/9CZt-tkJ0gY)
- [The Witcher 3 video](https://youtu.be/tu1LvzgK_9M)

## Showcase

### Fallout 4

- **Pair 1**
  - ![Fallout 4 OFF - 2026-06-24 15:01:53](https://reshade-images.s3.us-east-1.amazonaws.com/Fallout4+2026-06-24+15-01-53.png)
  - ![Fallout 4 ON - 2026-06-24 15:02:07](https://reshade-images.s3.us-east-1.amazonaws.com/Fallout4+2026-06-24+15-02-07.png)
- **Pair 2**
  - ![Fallout 4 OFF - 2026-08-12 11:33:45](https://reshade-images.s3.us-east-1.amazonaws.com/Fallout4+2026-08-12+11-33-45.png)
  - ![Fallout 4 ON - 2026-08-12 11:33:54](https://reshade-images.s3.us-east-1.amazonaws.com/Fallout4+2026-08-12+11-33-54.png)

### Watch Dogs

- **Pair 1**
  - ![Watch Dogs OFF - 2026-08-12 11:02:59](https://reshade-images.s3.us-east-1.amazonaws.com/Watch_Dogs+2026-08-12+11-04-13.png)
  - ![Watch Dogs ON - 2026-08-12 11:04:13](https://reshade-images.s3.us-east-1.amazonaws.com/Watch_Dogs+2026-08-12+11-02-59.png)
- **Pair 2**
  - ![Watch Dogs OFF - 2026-08-12 11:07:42](https://reshade-images.s3.us-east-1.amazonaws.com/Watch_Dogs+2026-08-12+11-07-42.png)
  - ![Watch Dogs ON - 2026-08-12 11:07:46](https://reshade-images.s3.us-east-1.amazonaws.com/Watch_Dogs+2026-08-12+11-07-46.png)
- **Pair 3**
  - ![Watch Dogs OFF - 2026-08-12 11:08:39](https://reshade-images.s3.us-east-1.amazonaws.com/Watch_Dogs+2026-08-12+11-08-39.png)
  - ![Watch Dogs ON - 2026-08-12 11:08:45](https://reshade-images.s3.us-east-1.amazonaws.com/Watch_Dogs+2026-08-12+11-08-45.png)
- **Pair 4**
  - ![Watch Dogs OFF - 2026-08-12 11:08:39](https://reshade-images.s3.us-east-1.amazonaws.com/Watch_Dogs+2026-08-12+11-14-16.png)
  - ![Watch Dogs ON - 2026-08-12 11:08:45](https://reshade-images.s3.us-east-1.amazonaws.com/Watch_Dogs+2026-08-12+11-14-20.png)

### The Witcher 3

- **Pair 1**
  - ![The Witcher 3 OFF - 2026-08-12 11:24:02](https://reshade-images.s3.us-east-1.amazonaws.com/witcher3+2026-08-12+11-24-02.png)
  - ![The Witcher 3 ON - 2026-08-12 11:25:21](https://reshade-images.s3.us-east-1.amazonaws.com/witcher3+2026-08-12+11-24-07.png)
- **Pair 2**
  - ![The Witcher 3 OFF - 2026-08-12 11:26:10](https://reshade-images.s3.us-east-1.amazonaws.com/witcher3+2026-08-12+11-26-10.png)
  - ![The Witcher 3 ON - 2026-08-12 11:26:10](https://reshade-images.s3.us-east-1.amazonaws.com/witcher3+2026-08-12+11-26-18.png)
- **Pair 3**
  - ![The Witcher 3 OFF - 2026-08-12 11:26:10](https://reshade-images.s3.us-east-1.amazonaws.com/witcher3+2026-08-12+11-20-54.png)
  - ![The Witcher 3 ON - 2026-08-12 11:26:10](https://reshade-images.s3.us-east-1.amazonaws.com/witcher3+2026-08-12+11-20-59.png)
