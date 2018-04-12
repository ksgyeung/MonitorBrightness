/*
Copyright (C) 2018 KSG Yeung

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#define IN_MB_DLL
#define _WIN32_DCOM
#include "mon_brightness.h"

#include <vector>
#include <memory>
#include <string>
#include <sstream>

#include <Windows.h>
#include <HighLevelMonitorConfigurationAPI.h>
#include <PhysicalMonitorEnumerationAPI.h>
#include <Winuser.h>
#include <comdef.h>
#include <wbemidl.h>

#include <Ntddvdeo.h>

#pragma comment(lib, "Dxva2.lib")
#pragma comment(lib, "wbemuuid.lib")

#define MB_MIN(a,b)		a<b?a:b
#define MB_MAGIC		171
#define MB_TYPE_NONE	0
#define MB_TYPE_DXVA2	1
#define MB_TYPE_WMI		2
#define MB_TYPE_IOCTL	3

template<class T> struct ComObjectDeleter
{
	template<class T> void operator ()(T* obj) const
	{
		IUnknown* unknown = (IUnknown*)obj;
		unknown->Release();
	}
};

struct MonitorStruct
{
	HMONITOR hMonitor;
	HDC hdcMonitor;
	RECT lprcMonitor;

	DWORD physical_monitor_count;
	std::vector<PHYSICAL_MONITOR> physical_monitors;
};

struct MBBaseStruct
{
public:
	unsigned char magic;
	unsigned char type;

	MBBaseStruct()
	{
		magic = MB_MAGIC;
	}
};

struct MBDxva2Struct : public MBBaseStruct
{
public:
	std::vector<PHYSICAL_MONITOR> physical_monitors;

	MBDxva2Struct()
	{
		type = MB_TYPE_DXVA2;
	}
};

struct MBWMIStruct: public MBBaseStruct
{
public:
	std::unique_ptr<IWbemLocator, ComObjectDeleter<IWbemLocator>> wbem_locator;
	std::unique_ptr<IWbemServices, ComObjectDeleter<IWbemServices>> wbem_services;

	std::unique_ptr<IWbemClassObject, ComObjectDeleter<IWbemClassObject>> clazz_obj;
	std::unique_ptr<IWbemClassObject, ComObjectDeleter<IWbemClassObject>> method;

	MBWMIStruct()
	{
		type = MB_TYPE_WMI;
	}
};

struct MBIoctlStruct : public MBBaseStruct
{
public:
	HANDLE lcd;

	MBIoctlStruct()
	{
		type = MB_TYPE_IOCTL;
	}
};

static std::wstring g_last_error;
static long g_com_init = 0;

static std::wstring GetLastErrorAsString(DWORD error)
{
	//Get the error message, if any.
	DWORD& errorMessageID = error;
	if (errorMessageID == 0)
		return std::wstring(); //No error message has been recorded

	LPWSTR messageBuffer = nullptr;
	size_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&messageBuffer, 0, NULL);

	std::wstring message(messageBuffer, size);

	//Free the buffer.
	LocalFree(messageBuffer);

	return message;
}

static std::wstring GetComErrorMessageWithHRESULT(HRESULT hr)
{
	_com_error com_error(hr);
	return com_error.ErrorMessage();
}

inline MBBaseStruct* mb_check_is_struct(void* handle)
{
	MBBaseStruct* base = (MBBaseStruct*)handle;
	if (base->magic != MB_MAGIC)
	{
		return nullptr;
	}
	return base;
}

MB_FUNCTION long MB_CONV mb_sum(long a, long b)
{
	return a + b;
}

MB_FUNCTION long MB_CONV mb_last_error(WCHAR* out_message, unsigned long length)
{
	if (out_message != nullptr)
	{
		memset(out_message, 0, sizeof(WCHAR) * length);
		memcpy(out_message, g_last_error.data(), MB_MIN(sizeof(WCHAR) * g_last_error.length(), sizeof(WCHAR) * length));
	}
	return (long)g_last_error.length();
}

MB_FUNCTION long MB_CONV mb_version()
{
	return MB_VERSION;
}

MB_FUNCTION long MB_CONV mb_dxva2_init(void** handle)
{
	std::vector<MonitorStruct> monitors;

	MONITORENUMPROC mep = (MONITORENUMPROC)[](HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) -> BOOL
	{
		std::vector<MonitorStruct>* monitors = (std::vector<MonitorStruct>*)dwData;

		MonitorStruct ms;
		ms.hMonitor = hMonitor;
		ms.hdcMonitor = hdcMonitor;
		ms.lprcMonitor = *lprcMonitor;
		monitors->push_back(ms);

		return TRUE;
	};
	if (!EnumDisplayMonitors(nullptr, nullptr, mep, (LPARAM)&monitors))
	{
		DWORD error = GetLastError();
		g_last_error = GetLastErrorAsString(error);
		return 0;
	}

	std::vector<PHYSICAL_MONITOR> physical_monitors_out;
	for (auto& ms : monitors)
	{
		if (!GetNumberOfPhysicalMonitorsFromHMONITOR(ms.hMonitor, &ms.physical_monitor_count))
		{
			DWORD error = GetLastError();
			g_last_error = GetLastErrorAsString(error);
			return 0;
		}

		std::unique_ptr<PHYSICAL_MONITOR[]> physical_monitors = std::make_unique<PHYSICAL_MONITOR[]>(ms.physical_monitor_count);
		if (!GetPhysicalMonitorsFromHMONITOR(ms.hMonitor, ms.physical_monitor_count, physical_monitors.get()))
		{
			DWORD error = GetLastError();
			g_last_error = GetLastErrorAsString(error);
			return 0;
		}

		DWORD capabilities;
		DWORD support_color_temp;
		for (auto i = 0u; i < ms.physical_monitor_count; i++)
		{
			capabilities = 0;
			support_color_temp = 0;

			PHYSICAL_MONITOR& physical_monitor = physical_monitors[i];
			if (!GetMonitorCapabilities(physical_monitor.hPhysicalMonitor, &capabilities, &support_color_temp))
			{
				continue;
			}

			if ((capabilities & MC_CAPS_BRIGHTNESS) == MC_CAPS_BRIGHTNESS)
			{
				ms.physical_monitors.push_back(physical_monitor);
				physical_monitors_out.push_back(physical_monitor);
			}
			else
			{
				DestroyPhysicalMonitors(1, &physical_monitor);
			}
		}
	}

	if (physical_monitors_out.size() == 0)
	{
		g_last_error = L"no brightness controllable monitors found";
	}

	if (handle != nullptr)
	{
		MBDxva2Struct* h = new MBDxva2Struct();
		h->physical_monitors = std::move(physical_monitors_out);
		*handle = h;
	}
	return 1;
}

MB_FUNCTION long MB_CONV mb_dxva2_get_count(void* handle, unsigned long* count)
{
	MBBaseStruct* base = mb_check_is_struct(handle);
	if (base == nullptr || base->type != MB_TYPE_DXVA2)
	{
		g_last_error = L"Invalid handle";
		return 0;
	}
	MBDxva2Struct* h = (MBDxva2Struct*)base;

	*count = (unsigned long)h->physical_monitors.size();
	return (long)h->physical_monitors.size();
}

MB_FUNCTION long MB_CONV mb_dxva2_set_brightness(void* handle, unsigned long index, double percent)
{
	MBBaseStruct* base = mb_check_is_struct(handle);
	if (base == nullptr || base->type != MB_TYPE_DXVA2)
	{
		g_last_error = L"Invalid handle";
		return 0;
	}
	MBDxva2Struct* h = (MBDxva2Struct*)base;

	if (percent < 0.0 || percent > 1.0)
	{
		g_last_error = L"percent out of range 0 .. 1";
		return 0;
	}

	if (h->physical_monitors.size() < index)
	{
		g_last_error = L"index out of range";
		return 0;
	}
	PHYSICAL_MONITOR& phyiscal_monitor = h->physical_monitors.at(index);

	DWORD min, max, current;
	if (!GetMonitorBrightness(phyiscal_monitor.hPhysicalMonitor, &min, &current, &max))
	{
		DWORD error = GetLastError();
		g_last_error = GetLastErrorAsString(error);
		return 0;
	}

	DWORD in_percent = (DWORD)ceil(min + (percent * max));

	BOOL ret = SetMonitorBrightness(phyiscal_monitor.hPhysicalMonitor, in_percent);
	if (!ret)
	{
		DWORD error = GetLastError();
		g_last_error = GetLastErrorAsString(error);
	}
	return ret;
}

MB_FUNCTION long MB_CONV mb_dxva2_get_brightness(void* handle, unsigned long index, double* percent)
{
	MBBaseStruct* base = mb_check_is_struct(handle);
	if (base == nullptr || base->type != MB_TYPE_DXVA2)
	{
		g_last_error = L"Invalid handle";
		return 0;
	}
	MBDxva2Struct* h = (MBDxva2Struct*)base;

	if (h->physical_monitors.size() < index)
	{
		g_last_error = L"monitor_index out of range";
		return 0;
	}
	PHYSICAL_MONITOR& phyiscal_monitor = h->physical_monitors.at(index);

	DWORD min, max, current;
	if (!GetMonitorBrightness(phyiscal_monitor.hPhysicalMonitor, &min, &current, &max))
	{
		DWORD error = GetLastError();
		g_last_error = GetLastErrorAsString(error);
		return 0;
	}

	double out_percent = (double)(current - min) / (double)(max - min);

	if (percent != nullptr)
	{
		*percent = out_percent;
	}
	return 1;
}

MB_FUNCTION long MB_CONV mb_dxva2_get_name(void* handle, unsigned long index, WCHAR* monitor_name, unsigned long max_length)
{
	MBBaseStruct* base = mb_check_is_struct(handle);
	if (base == nullptr || base->type != MB_TYPE_DXVA2)
	{
		g_last_error = L"Invalid handle";
		return 0;
	}
	MBDxva2Struct* h = (MBDxva2Struct*)base;

	if (h->physical_monitors.size() < index)
	{
		g_last_error = L"monitor_index out of range";
		return 0;
	}
	PHYSICAL_MONITOR& phyiscal_monitor = h->physical_monitors.at(index);

	if (monitor_name != nullptr)
	{
		memcpy(monitor_name, phyiscal_monitor.szPhysicalMonitorDescription, MB_MIN(sizeof(WCHAR) * wcslen(phyiscal_monitor.szPhysicalMonitorDescription), sizeof(WCHAR) * max_length));
	}
	return (long)wcslen(phyiscal_monitor.szPhysicalMonitorDescription);
}

MB_FUNCTION long MB_CONV mb_dxva2_cleanup(void* handle)
{
	MBBaseStruct* base = mb_check_is_struct(handle);
	if (base == nullptr || base->type != MB_TYPE_DXVA2)
	{
		g_last_error = L"Invalid handle";
		return 0;
	}
	MBDxva2Struct* h = (MBDxva2Struct*)base;

	for (auto& physical_monitor : h->physical_monitors)
	{
		DestroyPhysicalMonitors(1, &physical_monitor);
	}
	delete h;

	return 1;
}

MB_FUNCTION long MB_CONV mb_wmi_init(void** handle)
{
	std::unique_ptr<IWbemLocator, ComObjectDeleter<IWbemLocator>> wbem_locator;
	std::unique_ptr<IWbemServices, ComObjectDeleter<IWbemServices>> wbem_services;
	IWbemLocator* wbem_locator_receive = nullptr;
	IWbemServices* wbem_services_receive = nullptr;

	HRESULT hr;
	if (g_com_init == 0)
	{
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		hr = CoInitializeSecurity(
			NULL,                        // Security descriptor    
			-1,                          // COM negotiates authentication service
			NULL,                        // Authentication services
			NULL,                        // Reserved
			RPC_C_AUTHN_LEVEL_DEFAULT,   // Default authentication level for proxies
			RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation level for proxies
			NULL,                        // Authentication info
			EOAC_NONE,                   // Additional capabilities of the client or server
			NULL);                       // Reserved
	}
	g_com_init++;

	hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&wbem_locator_receive);
	wbem_locator = std::unique_ptr<IWbemLocator, ComObjectDeleter<IWbemLocator>>(wbem_locator_receive);
	if (FAILED(hr))
	{
		std::wstringstream ss;
		ss << std::hex << hr;

		g_last_error = L"CoCreateInstance(CLSID_WbemLocator, ... ) creation error with hr 0x" + ss.str() + L" " + GetComErrorMessageWithHRESULT(hr);
		return 0;
	}

	hr = wbem_locator->ConnectServer(BSTR(L"root\\wmi"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &wbem_services_receive);
	wbem_services = std::unique_ptr<IWbemServices, ComObjectDeleter<IWbemServices>>(wbem_services_receive);
	if (FAILED(hr))
	{
		std::wstringstream ss;
		ss << std::hex << hr;

		g_last_error = L"IWbemLocator->ConnectServer(...) return error with hr 0x" + ss.str() + L" " + GetComErrorMessageWithHRESULT(hr);
		return 0;
	}

	hr = CoSetProxyBlanket(wbem_services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
	if (FAILED(hr))
	{
		std::wstringstream ss;
		ss << std::hex << hr;

		g_last_error = L"CoSetProxyBlanket(IWbemServices*, ...) return error with 0x" + ss.str() + L" " + GetComErrorMessageWithHRESULT(hr);
		return 0;
	}

	std::unique_ptr<IWbemClassObject, ComObjectDeleter<IWbemClassObject>> clazz_obj = nullptr;
	IWbemClassObject* clazz_obj_receive = nullptr;
	hr = wbem_services->GetObjectW(BSTR("WmiMonitorBrightnessMethods"), 0, nullptr, &clazz_obj_receive, nullptr);
	clazz_obj = std::unique_ptr<IWbemClassObject, ComObjectDeleter<IWbemClassObject>>(clazz_obj_receive);
	if (FAILED(hr))
	{
		std::wstringstream ss;
		ss << std::hex << hr;

		g_last_error = L"IWbemServices->GetObjectW(...) return error with hr 0x" + ss.str() + L" " + GetComErrorMessageWithHRESULT(hr);
		return 0;
	}

	std::unique_ptr<IWbemClassObject, ComObjectDeleter<IWbemClassObject>> method = nullptr;
	IWbemClassObject* method_receive = nullptr;
	hr = clazz_obj->GetMethod(BSTR("WmiSetBrightness"), 0, &method_receive, nullptr);
	method = std::unique_ptr<IWbemClassObject, ComObjectDeleter<IWbemClassObject>>(method_receive);
	if (FAILED(hr))
	{
		clazz_obj->Release();

		std::wstringstream ss;
		ss << std::hex << hr;

		g_last_error = L"IWbemClassObject->GetMethod(...) return error with hr 0x" + ss.str() + L" " + GetComErrorMessageWithHRESULT(hr);
		return 0;
	}

	if (handle)
	{
		MBWMIStruct* h = new MBWMIStruct();
		h->wbem_locator = std::move(wbem_locator);
		h->wbem_services = std::move(wbem_services);

		h->clazz_obj = std::move(clazz_obj);
		h->method = std::move(method);

		*handle = h;
	}

	return 1;
}

MB_FUNCTION long MB_CONV mb_wmi_set_brightness(void* handle, uint32_t Timeout, uint8_t Brightness)
{
	MBBaseStruct* base = mb_check_is_struct(handle);
	if (base == nullptr || base->type != MB_TYPE_WMI)
	{
		g_last_error = L"Invalid handle";
		return 0;
	}
	MBWMIStruct* h = (MBWMIStruct*)base;

	HRESULT hr;
	std::unique_ptr<IWbemClassObject, ComObjectDeleter<IWbemClassObject>> instance = nullptr;
	IWbemClassObject* instance_receive;
	hr = h->clazz_obj->SpawnInstance(0, &instance_receive);
	instance = std::unique_ptr<IWbemClassObject, ComObjectDeleter<IWbemClassObject>>(instance_receive);
	if (FAILED(hr))
	{
		std::wstringstream ss;
		ss << std::hex << hr;

		g_last_error = L"IWbemClassObject->SpawnInstance(...) return error with hr 0x" + ss.str() + L" " + GetComErrorMessageWithHRESULT(hr);
		return 0;
	}

	_variant_t param1(Timeout);
	_variant_t param2(Brightness);

	instance->Put(BSTR("Timeout"), 0, &param1, CIM_UINT32);
	instance->Put(BSTR("Brightness"), 0, &param2, CIM_UINT8);

	IWbemClassObject* out_params = nullptr;
	hr = h->wbem_services->ExecMethod(BSTR("WmiMonitorBrightnessMethods"), BSTR("WmiSetBrightness"), 0, nullptr, instance.get(), &out_params, nullptr);
	if (FAILED(hr))
	{
		std::wstringstream ss;
		ss << std::hex << hr;

		g_last_error = L"IWbemServices->ExecMethod(...) return error with hr 0x" + ss.str() + L" " + GetComErrorMessageWithHRESULT(hr);
		return 0;
	}

	_variant_t ret;
	out_params->Get(_bstr_t(L"ReturnValue"), 0, &ret, nullptr, nullptr);
	out_params->Release();

	return ret.uintVal;
}

MB_FUNCTION long MB_CONV mb_wmi_cleanup(void* handle)
{
	MBBaseStruct* base = mb_check_is_struct(handle);
	if (base == nullptr || base->type != MB_TYPE_WMI)
	{
		g_last_error = L"Invalid handle";
		return 0;
	}
	delete base;

	return 1;
}

MB_FUNCTION long MB_CONV mb_ioctl_init(void** handle)
{
	if (handle != nullptr)
	{
		*handle = nullptr;
	}

	HANDLE lcd = CreateFileW(L"\\\\.\\LCD", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (lcd == INVALID_HANDLE_VALUE)
	{
		DWORD error = GetLastError();
		g_last_error = GetLastErrorAsString(error);

		return 0;
	}

	BYTE support_brightness[256] = { 0 };
	DWORD support_brightness_ret = 0;
	if (!DeviceIoControl(lcd, IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS, nullptr, 0, support_brightness, 256, &support_brightness_ret, nullptr))
	{
		DWORD error = GetLastError();
		g_last_error = GetLastErrorAsString(error);

		return 0;
	}
	if (support_brightness_ret == 0)
	{
		g_last_error = L"Monitor found but not support for setting brightness";
		return 0;
	}

	if (handle == nullptr)
	{
		g_last_error = L"function succeeded, but handle is nullptr";
	}
	else
	{
		MBIoctlStruct* h = new MBIoctlStruct();
		h->lcd = lcd;

		*handle = h;
	}
	return 1;
}

MB_FUNCTION long MB_CONV mb_ioctl_set_brightness(void* handle, unsigned long ac_percent, unsigned long dc_percent)
{
	MBBaseStruct* base = mb_check_is_struct(handle);
	if (base == nullptr || base->type != MB_TYPE_IOCTL)
	{
		g_last_error = L"Invalid handle";
		return 0;
	}
	MBIoctlStruct* h = (MBIoctlStruct*)base;

	if (ac_percent > 100)
	{
		g_last_error = L"ac_percent out of range 0 .. 100";
		return 0;
	}
	if (dc_percent > 100)
	{
		g_last_error = L"dc_percent out of range 0 .. 100";
		return 0;
	}

	HANDLE& lcd = h->lcd;

	DISPLAY_BRIGHTNESS db = { 0 };
	db.ucDisplayPolicy = DISPLAYPOLICY_BOTH;
	db.ucACBrightness = (UCHAR)ac_percent;
	db.ucDCBrightness = (UCHAR)dc_percent;
	DWORD db_size = sizeof(DISPLAY_BRIGHTNESS);
	
	BOOL ret = DeviceIoControl(lcd, IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS, &db, db_size, nullptr, 0, nullptr, nullptr);
	if (ret)
	{
		DWORD error = GetLastError();
		g_last_error = GetLastErrorAsString(error);

		return 0;
	}

	return 1;
}

MB_FUNCTION long MB_CONV mb_ioctl_get_brightness(void* handle, unsigned long* ac_percent, unsigned long* dc_percent)
{
	MBBaseStruct* base = mb_check_is_struct(handle);
	if (base == nullptr || base->type != MB_TYPE_IOCTL)
	{
		g_last_error = L"Invalid handle";
		return 0;
	}
	MBIoctlStruct* h = (MBIoctlStruct*)base;
	HANDLE& lcd = h->lcd;

	DISPLAY_BRIGHTNESS db;
	DWORD db_size = sizeof(DISPLAY_BRIGHTNESS);
	DWORD db_ret = 0;

	BOOL ret = DeviceIoControl(lcd, IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS, nullptr, 0, &db, db_size, &db_ret, nullptr);
	if (!ret)
	{
		DWORD error = GetLastError();
		g_last_error = GetLastErrorAsString(error);

		return 0;
	}
	if (db_ret == 0)
	{
		g_last_error = L"db_ret is zero";
		return 0;
	}

	if (ac_percent == nullptr)
	{
		g_last_error = L"function succeeded, but ac_percent is nullptr";
	}
	else
	{
		*ac_percent = db.ucACBrightness;
	}

	if (dc_percent == nullptr)
	{
		g_last_error = L"function succeeded, but dc_percent is nullptr";
	}
	else
	{
		*dc_percent = db.ucDCBrightness;
	}

	return 1;
}

MB_FUNCTION long MB_CONV mb_ioctl_cleanup(void* handle)
{
	MBBaseStruct* base = mb_check_is_struct(handle);
	if (base == nullptr || base->type != MB_TYPE_IOCTL)
	{
		g_last_error = L"Invalid handle";
		return 0;
	}
	MBIoctlStruct* h = (MBIoctlStruct*)base;
	HANDLE& lcd = h->lcd;
	CloseHandle(lcd);
	delete h;

	return 1;
}
