#include "hidapi.h"

#include <vector>
#include <emscripten.h>
#include <emscripten/val.h>

using namespace emscripten;
using namespace std;

struct hid_device_ {
	val device_handle;
	int blocking;
	int uses_numbered_reports;
	string last_error_str;
};

static string  last_global_error_str = "";

static void register_global_error(string msg)
{
	last_global_error_str = msg;
}

static void register_device_error(hid_device *dev, string msg)
{
	dev->last_error_str = msg;
}

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
	dev->last_error_str = "";

	return dev;
}

int HID_API_EXPORT hid_init(void)
{
    register_global_error("");
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
        cur_dev->product_id = usb_device["productId"].as<uint8_t>();
        cur_dev->vendor_id = usb_device["productId"].as<uint8_t>();
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
		register_global_error("Device with requested VID/PID/(SerialNumber) not found");
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
		register_global_error("hid_device allocation error");
		return NULL;
	}

    dev->device_handle = usb_device;

    return dev;
}

void HID_API_EXPORT hid_close(hid_device *dev)
{
	if (!dev)
		return;

	auto result = dev->device_handle.call<val>("close").await();

	/* Free the device error message */
	register_device_error(dev, "");

	free(dev);
}

int HID_API_EXPORT HID_API_CALL hid_get_feature_report(hid_device *dev, unsigned char *data, size_t length)
{
    auto result = dev->device_handle.call<val>("receiveFeatureReport", data[0]).await();
    vector<unsigned char> result_data = result.as<vector<unsigned char>>();

    if(result_data.size() != length) {
        register_global_error("hid_get_feature_report report length not matching");
        return -1;
    }

    for(int i=0; i<length; i++) {
        data[i] = result_data.at(i);
    }

    return 0;
}