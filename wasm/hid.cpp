#include "hidapi.h"

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <emscripten.h>
#include <emscripten/val.h>
#include <emscripten/bind.h>

using namespace emscripten;
using namespace std;

typedef std::function<void(uint8_t reportId, std::vector<uint8_t> data)> HidReadCallback;

struct hid_device_ {
	val device_handle;
	int blocking;
	int uses_numbered_reports;
	wstring last_error_str;
};

HidReadCallback global_hid_read_callback;
static wstring last_global_error_str = L"";

static void register_global_error(wstring msg)
{
	std::string msg2( msg.begin(), msg.end() );
	cout << msg2 << endl;

	last_global_error_str = msg;
}

static void register_device_error(hid_device *dev, wstring msg)
{
	dev->last_error_str = msg;
}

std::mutex callbackMutex;

extern "C" {
void module_export() {
	cout << "Hehe\n";
}
}

void hid_read_callback(emscripten::val device){
	std::lock_guard<std::mutex> lock(callbackMutex);
	cout << "Hehe\n";

	/// more code just trying to log something to show it works
	/*
	uint8_t reportId = e["reportId"].as<uint8_t>();
	std::vector<uint8_t> data;
	
	int result_length = e["data"]["byteLength"].as<int>();
	for(int i=0; i<result_length-1; i++) {
        data.push_back(e["data"].call<uint8_t>("getUint8", i));
    }
	*/

	// global_hid_read_callback(reportId, data);
	//cout << "Got data\n";
}

EMSCRIPTEN_BINDINGS(events){
  	emscripten::function("hid_read_callback", hid_read_callback);
}

EM_JS(void, add_device_input_callback, (EM_VAL deviceHandle, void* usbHandle), {
	device = Emval.toValue(deviceHandle);
	console.log(device, usbHandle);

	device.addEventListener("inputreport", (event) => {
		// Module.hid_read_callback(device);
		Module.ccall("module_export", 'void', []);

		// callbackHandle(device, event);
		// const { data, device, reportId } = event;
		// console.log(data);
	});
});

static wchar_t *utf8_to_wchar_t(const char *utf8)
{
	wchar_t *ret = NULL;

	if (utf8) {
		size_t wlen = mbstowcs(NULL, utf8, 0);
		if ((size_t) -1 == wlen) {
			return wcsdup(L"");
		}
		ret = (wchar_t*) calloc(wlen+1, sizeof(wchar_t));
		if (ret == NULL) {
			/* as much as we can do at this point */
			return NULL;
		}
		mbstowcs(ret, utf8, wlen+1);
		ret[wlen] = 0x0000;
	}

	return ret;
}

static hid_device *new_hid_device(void)
{
	hid_device *dev = (hid_device*) calloc(1, sizeof(hid_device));
	dev->device_handle = val::null();
	dev->blocking = 1;
	dev->uses_numbered_reports = 0;
	dev->last_error_str = L"";

	return dev;
}

int HID_API_EXPORT hid_init(void)
{
    register_global_error(L"");
	return 0;
}

int HID_API_EXPORT hid_exit(void)
{
	return 0;
}

struct hid_device_info  HID_API_EXPORT *hid_enumerate(unsigned short vendor_id, unsigned short product_id)
{
    struct hid_device_info *root = NULL;
    struct hid_device_info *cur_dev = NULL;
    struct hid_device_info *tmp = NULL;

    auto usb_devices = val::global("navigator")["hid"].call<val>("getDevices").await();
    uint8_t devices_num = usb_devices["length"].as<uint8_t>();
    for (uint8_t i = 0; i < devices_num; i++) {
        auto usb_device = usb_devices[i];

        tmp = (struct hid_device_info*) calloc(1, sizeof(struct hid_device_info));
        if (cur_dev) {
            cur_dev->next = tmp;
        }
        else {
            root = tmp;
        }
        cur_dev = tmp;

        cur_dev->next = NULL;
        cur_dev->product_id = usb_device["productId"].as<unsigned short>();
        cur_dev->vendor_id = usb_device["vendorId"].as<unsigned short>();

        string productName = usb_device["productName"].as<string>();
        cur_dev->product_string = utf8_to_wchar_t(productName.c_str());

        string path = to_string(i);
        cur_dev->path = (char*)calloc(path.length(), sizeof(char));
        path.copy(cur_dev->path, path.length());
    }
    
    return root;
}

void  HID_API_EXPORT HID_API_CALL hid_free_enumeration(struct hid_device_info *devs)
{
	/* TODO: Merge this with the Linux version. This function is platform-independent. */
	struct hid_device_info *d = devs;
	while (d) {
		struct hid_device_info *next = d->next;
		free(d->path);
		free(d->serial_number);
		free(d->manufacturer_string);
		free(d->product_string);
		free(d);
		d = next;
	}
}

hid_device * hid_open(unsigned short vendor_id, unsigned short product_id, const wchar_t *serial_number)
{
	struct hid_device_info *devs, *cur_dev;
	const char *path_to_open = NULL;
	hid_device *handle = NULL;

	/* register_global_error: global error is reset by hid_enumerate/hid_init */
	devs = hid_enumerate(vendor_id, product_id);
	if (devs == NULL) {
		/* register_global_error: global error is already set by hid_enumerate */
		return NULL;
	}

	cur_dev = devs;
	while (cur_dev) {
		if (cur_dev->vendor_id == vendor_id &&
		    cur_dev->product_id == product_id) {
			if (serial_number) {
				if (wcscmp(serial_number, cur_dev->serial_number) == 0) {
					path_to_open = cur_dev->path;
					break;
				}
			}
			else {
				path_to_open = cur_dev->path;
				break;
			}
		}
		cur_dev = cur_dev->next;
	}

	if (path_to_open) {
		/* Open the device */
		handle = hid_open_path(path_to_open);
	} else {
		register_global_error(L"Device with requested VID/PID/(SerialNumber) not found");
	}

	hid_free_enumeration(devs);

	return handle;
}

HID_API_EXPORT hid_device * HID_API_CALL hid_open_path(const char *path)
{
    auto usb_devices = val::global("navigator")["hid"].call<val>("getDevices").await();
    uint8_t devices_num = usb_devices["length"].as<uint8_t>();
    int num = stoi(path);
    if(num >= devices_num)
        return NULL;

    auto usb_device = usb_devices[num];
    auto result = usb_device.call<val>("open").await();

    hid_device *dev = new_hid_device();
    if (dev == NULL) {
		register_global_error(L"hid_device allocation error");
		return NULL;
	}

	// usb_device.call<void>("addEventListener", std::string("inputreport"), val::module_property("hid_read_callback"));

    dev->device_handle = usb_device;
	add_device_input_callback(usb_device.as_handle(), (void*)dev);
	
    return dev;
}

void HID_API_EXPORT hid_close(hid_device *dev)
{
	if (!dev)
		return;

	auto result = dev->device_handle.call<val>("close").await();

	/* Free the device error message */
	register_device_error(dev, L"");

	free(dev);
}

int HID_API_EXPORT HID_API_CALL hid_get_feature_report(hid_device *dev, unsigned char *data, size_t length)
{
    auto result = dev->device_handle.call<val>("receiveFeatureReport", data[0]).await();
    int result_length = result["byteLength"].as<int>();

    // if(result_length != length) {
	// 	printf("%d, %d", result_length, length);
    //     register_global_error(L"hid_get_feature_report report length not matching");
    //     return -1;
    // }

	
    for(int i=0; i<result_length-1; i++) {
        data[i] = result.call<uint8_t>("getUint8", i);
    }

    return result_length;
}

int HID_API_EXPORT HID_API_CALL hid_send_feature_report(hid_device *dev, const unsigned char *data, size_t length)
{
	auto result = dev->device_handle.call<val>(
		"sendFeatureReport",
		data[0],
		val(typed_memory_view(length-1, data))
	).await();

    return length;
}

const wchar_t * HID_API_EXPORT HID_API_CALL hid_error(hid_device *dev)
{
	if (dev) {
		if (dev->last_error_str.length() == 0)
			return L"Success";
		return (wchar_t*)dev->last_error_str.c_str();
	}

	// Global error messages are not (yet) implemented on Windows.
	return L"hid_error for global errors is not implemented yet";
}

int HID_API_EXPORT HID_API_CALL hid_read(hid_device *dev, unsigned char *data, size_t length)
{
	return hid_get_feature_report(dev, data, length);
}


int HID_API_EXPORT HID_API_CALL hid_read_register(HidReadCallback callback)
{
	global_hid_read_callback = callback;
}

int HID_API_EXPORT HID_API_CALL hid_write(hid_device *dev, const unsigned char *data, size_t length)
{
	// vector<unsigned char> report_data(length);
	// for(int x=0; x<length; x++) {
	// 	report_data[x] = data[x+1];
	// }

	// auto result = dev->device_handle.call<val>("sendReport", data[0], report_data).await();

    return 0;
}

int HID_API_EXPORT HID_API_CALL hid_set_nonblocking(hid_device *dev, int nonblock)
{
	return 0; /* Not supported by webhid */
}