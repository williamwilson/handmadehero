#include <windows.h>
#include <stdint.h>
#include <stdio.h>

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

    /* todo: this is where we would force paint */
    RenderWeirdGradient(&buffer, xOffset++, yOffset++);

    HDC deviceContext = GetDC(hwnd);
    window_dimensions dimensions = GetWindowDimensions(hwnd);
    CopyBufferToWindow(&buffer, deviceContext, 0, 0, dimensions.width, dimensions.height);
    ReleaseDC(hwnd, deviceContext);
  }

  return 0;
}
