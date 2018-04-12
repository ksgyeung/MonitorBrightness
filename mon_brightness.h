#pragma once
/*
Copyright (C) 2018 KSG Yeung

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stddef.h>
#include <stdint.h>
#include <Windows.h>

#ifdef IN_MB_DLL
#define MB_FUNCTION __declspec(dllexport)
#else
#define MB_FUNCTION __declspec(dllimport)
#endif

#define MB_DEPRECATED
#define MB_CONV __stdcall

#define MB_VERSION							5

#ifdef __cplusplus
extern "C"
{
#endif
	/*
	Sum 2 numbers
	=========================================
	return: a + b
	*/
	MB_FUNCTION long MB_CONV mb_sum(long a, long b);

	/*
	Get the reason of failure
	=========================================
	out_message: the error message (caller must alloc memory first!)
	length: out_message max size
	return: the error message length
	*/
	MB_FUNCTION long MB_CONV mb_last_error(WCHAR* out_message, unsigned long length);

	/*
	Get the version of this library
	*/
	MB_FUNCTION long MB_CONV mb_version();

	/*
	Init dxva2 resources,  this function must call before calling any other dxva2 functions
	*/
	MB_FUNCTION long MB_CONV mb_dxva2_init(void** handle);

	/*
	Get brightness controllable monitors count
	*/
	MB_FUNCTION long MB_CONV mb_dxva2_get_count(void* handle, unsigned long* count);

	/*
	Set monitor brightness
	*/
	MB_FUNCTION long MB_CONV mb_dxva2_set_brightness(void* handle, unsigned long index, double percent);

	/*
	Get monitor brightness
	*/
	MB_FUNCTION long MB_CONV mb_dxva2_get_brightness(void* handle, unsigned long index, double* percent);

	/*
	Get monitor name
	*/
	MB_FUNCTION long MB_CONV mb_dxva2_get_name(void* handle, unsigned long index, WCHAR* monitor_name, unsigned long max_length);

	/*
	Clean up and release resources
	*/
	MB_FUNCTION long MB_CONV mb_dxva2_cleanup(void* handle);

	/*
	Init WMI resources,  this function must call before calling any other WMI functions
	*/
	MB_FUNCTION long MB_CONV mb_wmi_init(void** handle);

	/*
	Set monitor brightness
	*/
	MB_FUNCTION long MB_CONV mb_wmi_set_brightness(void* handle, uint32_t Timeout, uint8_t Brightness);

	/*
	Clean up and release resources
	*/
	MB_FUNCTION long MB_CONV mb_wmi_cleanup(void* handle);

	/*
	Init IOCTL resources,  this function must call before calling any other IOCTL functions
	*/
	MB_FUNCTION long MB_CONV mb_ioctl_init(void** handle);
	
	/*
	See: mb_ioctl_init(void**)
	*/
	MB_DEPRECATED MB_FUNCTION long MB_CONV mb_ioctl_search_lcd(void** handle);

	/*
	Set monitor brightness
	*/
	MB_FUNCTION long MB_CONV mb_ioctl_set_brightness(void* handle, unsigned long ac_percent, unsigned long dc_percent);

	/*
	See: mb_ioctl_set_brightness(void*,unsigned long,unsigned long)
	*/
	MB_DEPRECATED MB_FUNCTION long MB_CONV mb_ioctl_set_lcd_brightness(void* handle, unsigned long ac_percent, unsigned long dc_percent);

	/*
	Get monitor brightness
	*/
	MB_FUNCTION long MB_CONV mb_ioctl_get_brightness(void* handle, unsigned long* ac_percent, unsigned long* dc_percent);

	/*
	See: mb_ioctl_get_brightness(void*,unsigned long*,unsigned long*)
	*/
	MB_DEPRECATED MB_FUNCTION long MB_CONV mb_ioctl_get_lcd_brightness(void* handle, unsigned long* ac_percent, unsigned long* dc_percent);

	/*
	Clean up and release resources
	*/
	MB_FUNCTION long MB_CONV mb_ioctl_cleanup(void* handle);

	/*
	See: mb_ioctl_cleanup(void*)
	*/
	MB_DEPRECATED MB_FUNCTION long MB_CONV mb_ioctl_close_lcd(void* handle);

#ifdef __cplusplus
}
#endif
