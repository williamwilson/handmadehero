#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <dsound.h>

#define PI 3.14159265f

static bool running;
struct offscreen_buffer
{
  BITMAPINFO bitmapInfo;
  void *buffer;
  int32_t width;
  int32_t height;
  int32_t bytesPerPixel;
  int32_t pitch;
};
static offscreen_buffer buffer;
struct sound_parameters
{
  int32_t samplesPerSecond;
  int32_t numberOfChannels;
  int32_t bytesPerSample;
  int32_t toneHz;
  int32_t samplesPerPeriod;
  int32_t volume;
  int32_t bufferSize;
};

struct sound_output
{
  sound_parameters *parameters;
  LPDIRECTSOUNDBUFFER primaryBuffer;
  LPDIRECTSOUNDBUFFER secondaryBuffer;
  uint32_t runningSampleIndex;
};
static sound_output soundOutput;

struct window_dimensions
{
  int32_t width;
  int32_t height;
};

static window_dimensions GetWindowDimensions(HWND hwnd)
{
  RECT clientRect;
  GetClientRect(hwnd, &clientRect);

  window_dimensions dimensions;
  dimensions.width = clientRect.right - clientRect.left;
  dimensions.height = clientRect.bottom - clientRect.top;
  return dimensions;
};

static void RenderWeirdGradient(offscreen_buffer *buffer, int32_t blueOffset, int32_t greenOffset)
{
  uint8_t *row = (uint8_t *)buffer->buffer;
  for (int32_t y = 0; y < buffer->height; y++)
  {
    uint32_t *pixel = (uint32_t *)row;
    for (int32_t x = 0; x < buffer ->width; x++)
    {
      uint8_t blue = x + blueOffset;
      uint8_t green = y + greenOffset;
      *pixel = ((green << 8) | blue);
      pixel++;
    }
    row += buffer->pitch;
  }
}

static void ResizeDIBSection(offscreen_buffer *buffer, int32_t width, int32_t height)
{
  if (buffer->buffer)
  {
    VirtualFree(buffer->buffer, 0, MEM_RELEASE);
  }

  buffer->width = width;
  buffer->height = height;
  buffer->bytesPerPixel = 4;

  buffer->bitmapInfo.bmiHeader.biSize = sizeof(buffer->bitmapInfo.bmiHeader);
  buffer->bitmapInfo.bmiHeader.biWidth = buffer->width;
  buffer->bitmapInfo.bmiHeader.biHeight = -(buffer->height);
  buffer->bitmapInfo.bmiHeader.biPlanes = 1;
  buffer->bitmapInfo.bmiHeader.biBitCount = 32;
  buffer->bitmapInfo.bmiHeader.biCompression = BI_RGB;

  int32_t bitmapMemorySize = buffer->width * buffer->height * buffer->bytesPerPixel;
  buffer->buffer = VirtualAlloc(0, bitmapMemorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

  buffer->pitch = buffer->width * buffer->bytesPerPixel;
}

static void CopyBufferToWindow(offscreen_buffer *buffer, HDC deviceContext, int32_t x, int32_t y, int32_t width, int32_t height)
{
  /* todo: aspect ratio correction */
  StretchDIBits(deviceContext,
    /* X, Y, Width, Height */
    x, y, width, height,
    0, 0, buffer->width, buffer->height,
    buffer->buffer,
    &buffer->bitmapInfo,
    DIB_RGB_COLORS, SRCCOPY);
}

#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter);
typedef DIRECT_SOUND_CREATE(direct_sound_create);

static void InitDSound(HWND hwnd, sound_output *output)
{
  HMODULE directSoundLibrary = LoadLibraryA("dsound.dll");
  if (!directSoundLibrary)
  {
    OutputDebugStringA("Failed to load dsound.dll\n");
    return;
  }

  direct_sound_create *DirectSoundCreate = (direct_sound_create *)GetProcAddress(directSoundLibrary, "DirectSoundCreate");
  if (!DirectSoundCreate)
  {
    OutputDebugStringA("Failed to get procedure address for DirectSoundCreate\n");
    return;
  }

  LPDIRECTSOUND directSound;
  if (!SUCCEEDED(DirectSoundCreate(NULL, &directSound, NULL)))
  {
    OutputDebugStringA("Failed to create direct sound\n");
    return;
  }

  if (!SUCCEEDED(directSound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY)))
  {
    OutputDebugStringA("Failed to set cooperative level\n");
    return;
  }

  DSBUFFERDESC bufferDescription = {};
  bufferDescription.dwSize = sizeof(bufferDescription);
  bufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;

  if (!SUCCEEDED(directSound->CreateSoundBuffer(&bufferDescription, &output->primaryBuffer, 0)))
  {
    OutputDebugStringA("Failed to create primary sound buffer\n");
    return;
  }

  WAVEFORMATEX waveFormat;
  waveFormat.wFormatTag = WAVE_FORMAT_PCM;
  waveFormat.nChannels = 2;
  waveFormat.nSamplesPerSec = output->parameters->samplesPerSecond;
  waveFormat.wBitsPerSample = 16;
  waveFormat.nBlockAlign = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
  waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
  waveFormat.cbSize = 0;
  if (!SUCCEEDED(output->primaryBuffer->SetFormat(&waveFormat)))
  {
    OutputDebugStringA("Failed to set format on primary buffer\n");
    return;
  }

  DSBUFFERDESC secondaryBufferDescription = {};
  secondaryBufferDescription.dwSize = sizeof(bufferDescription);
  secondaryBufferDescription.dwFlags = DSBCAPS_GETCURRENTPOSITION2;
  secondaryBufferDescription.dwBufferBytes = output->parameters->bufferSize;
  secondaryBufferDescription.lpwfxFormat = &waveFormat;

  if (!SUCCEEDED(directSound->CreateSoundBuffer(&secondaryBufferDescription, &output->secondaryBuffer, 0)))
  {
    OutputDebugStringA("Failed to create secondary sound buffer\n");
    return;
  }
}

static void WriteSineWaveToBuffer(sound_output *output, VOID *region, DWORD regionSize)
{
  int16_t *regionSample = (int16_t *)region;
  DWORD regionSampleCount = regionSize / output->parameters->bytesPerSample;
  for (int32_t sampleIndex = 0;
    sampleIndex < regionSampleCount;
    sampleIndex++)
  {
    int16_t sampleValue = (int16_t)(sinf(((float)output->runningSampleIndex / (float)output->parameters->samplesPerPeriod) * 2 * PI) * output->parameters->volume);
    *regionSample++ = sampleValue;
    *regionSample++ = sampleValue;
    output->runningSampleIndex++;
  }
}

static void FillSoundBuffer(sound_output *output, DWORD byteIndexToLock, DWORD bytesToWrite)
{
  VOID *region1;
  DWORD region1Size;
  VOID *region2;
  DWORD region2Size;
  if (SUCCEEDED(output->secondaryBuffer->Lock(byteIndexToLock, bytesToWrite,
                                      &region1, &region1Size,
                                      &region2, &region2Size,
                                      0)))
  {
    WriteSineWaveToBuffer(output, region1, region1Size);
    WriteSineWaveToBuffer(output, region2, region2Size);

    if (!SUCCEEDED(output->secondaryBuffer->Unlock(region1, region1Size, region2, region2Size)))
    {
      OutputDebugStringA("Failed to unlock region(s)\n");
    }
  }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_SIZE:
    {
      //window_dimensions dimensions = GetWindowDimensions(hwnd);
      //ResizeDIBSection(&buffer, dimensions.width, dimensions.height);
    }
    return 0;
    case WM_DESTROY:
      running = false;
      return 0;
    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC deviceContext = BeginPaint(hwnd, &ps);
      window_dimensions dimensions = GetWindowDimensions(hwnd);
      CopyBufferToWindow(&buffer, deviceContext, 0, 0, dimensions.width, dimensions.height);
      EndPaint(hwnd, &ps);
    }
    return 0;
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

INT WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
  PSTR lpCmdLine, INT nCmdShow)
{
  ResizeDIBSection(&buffer, 1280, 720);
  const char CLASS_NAME[] = "Sample Window Class";

  WNDCLASS wc = {};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;

  RegisterClass(&wc);

  HWND hwnd = CreateWindowEx(
    0,
    CLASS_NAME,
    "Test Window",
    WS_OVERLAPPEDWINDOW | WS_VISIBLE,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    NULL,
    NULL,
    hInstance,
    NULL);

  if (!hwnd)
  {
    return 0;
  }

  ShowWindow(hwnd, nCmdShow);

  sound_parameters soundParams = {};
  soundParams.samplesPerSecond = 48000;
  soundParams.numberOfChannels = 2;
  soundParams.bytesPerSample = sizeof(int16_t) * soundParams.numberOfChannels;
  soundParams.toneHz = 262;
  soundParams.samplesPerPeriod = soundParams.samplesPerSecond / soundParams.toneHz;
  soundParams.volume = 3000;
  soundParams.bufferSize = soundParams.samplesPerSecond * soundParams.bytesPerSample;

  sound_output soundOutput = {};
  soundOutput.parameters = &soundParams;
  InitDSound(hwnd, &soundOutput);
  FillSoundBuffer(&soundOutput, 0, soundParams.bufferSize);
  if (!SUCCEEDED(soundOutput.secondaryBuffer->Play(0, 0, DSBPLAY_LOOPING)))
  {
    OutputDebugStringA("Failed to play buffer\n");
  }

  MSG msg = { };
  running = true;

  int32_t xOffset = 0;
  int32_t yOffset = 0;
  while (running)
  {
    while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE))
    {
      if (msg.message == WM_QUIT)
      {
        running = false;
      }

      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    RenderWeirdGradient(&buffer, xOffset++, yOffset++);

    /* note: direct sound test */
    DWORD playCursor;
    DWORD writeCursor;
    if (SUCCEEDED(soundOutput.secondaryBuffer->GetCurrentPosition(&playCursor, &writeCursor)))
    {
      DWORD byteIndexToLock = (soundOutput.runningSampleIndex * soundParams.bytesPerSample) % soundParams.bufferSize;
      DWORD bytesToWrite;
      /* todo: update this to use a lower latency offset from the play cursor */
      if (byteIndexToLock > playCursor)
      {
        bytesToWrite = (soundParams.bufferSize - byteIndexToLock) + playCursor;
      }
      else
      {
        bytesToWrite = playCursor - byteIndexToLock;
      }

      FillSoundBuffer(&soundOutput, byteIndexToLock, bytesToWrite);
    }

    HDC deviceContext = GetDC(hwnd);
    window_dimensions dimensions = GetWindowDimensions(hwnd);
    CopyBufferToWindow(&buffer, deviceContext, 0, 0, dimensions.width, dimensions.height);
    ReleaseDC(hwnd, deviceContext);
  }

  return 0;
}
