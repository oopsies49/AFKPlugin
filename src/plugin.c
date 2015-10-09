/*
 * TeamSpeak 3 demo plugin
 *
 * Copyright (c) 2008-2014 TeamSpeak Systems GmbH
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/IOHIDShared.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <unistd.h>
#include <pthread.h>
#endif

#include "public_errors.h"
#include "public_errors_rare.h"
#include "public_definitions.h"
#include "public_rare_definitions.h"
#include "ts3_functions.h"
#include "plugin.h"


#ifdef _WIN32
#define _strcpy(dest, destSize, src) strcpy_s(dest, destSize, src)
#define snprintf sprintf_s
#else
#define _strcpy(dest, destSize, src) { strncpy(dest, src, destSize-1); (dest)[destSize-1] = '\0'; }
#endif

#define PLUGIN_API_VERSION 20

#define PATH_BUFSIZE 512
#define COMMAND_BUFSIZE 128
#define INFODATA_BUFSIZE 128
#define SERVERINFO_BUFSIZE 256
#define CHANNELINFO_BUFSIZE 512
#define RETURNCODE_BUFSIZE 128

static struct TS3Functions ts3Functions;
static char* pluginID = NULL;

#ifdef _WIN32

#else
static mach_port_t __idle_osx_master_port;
io_registry_entry_t __idle_osx_service;

static pthread_t idle_loop_thread;
pthread_mutex_t idle_time_mutex;
#endif

static uint64 max_idle_time = 600; // Seconds

const uint64 ACTIVITY_CHECK_RESOLUTION = 5; // Seconds
const uint64 MIN_IDLE_TIME = 15; // Seconds

/* Unique name identifying this plugin */
const char* ts3plugin_name() {
	return "AFKPlugin";
}

/* Plugin version */
const char* ts3plugin_version() {
    return "0.1";
}

/* Plugin API version. Must be the same as the clients API major version, else the plugin fails to load. */
int ts3plugin_apiVersion() {
	return PLUGIN_API_VERSION;
}

/* Plugin author */
const char* ts3plugin_author() {
    return "oopsies49";
}

/* Plugin description */
const char* ts3plugin_description() {
    return "This plugin toggles the away status after a set amount of idle time.";
}

/* Set TeamSpeak 3 callback functions */
void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
    ts3Functions = funcs;
}

/*
 * Custom code called right after loading the plugin. Returns 0 on success, 1 on failure.
 * If the function returns 1 on failure, the plugin will be unloaded again.
 */
int ts3plugin_init() {
    printf("PLUGIN: init\n");

#ifdef _WIN32
	return 0;
#else
		pthread_mutex_init(&idle_time_mutex, NULL);
		init_idle();

		if (pthread_create(&idle_loop_thread, NULL, idle_loop, NULL)){
			printf("PLUGIN: failed creating idle loop thread\n");
			return 1;
		} else {
			printf("PLUGIN: idle loop thread created\n");
		}
#endif

}

/* Custom code called right before the plugin is unloaded */
void ts3plugin_shutdown() {
    /* Your plugin cleanup code here */
    printf("PLUGIN: shutdown\n");

#ifdef _WIN32

#else
		printf("Cancelling idle loop thread\n");
		pthread_cancel(idle_loop_thread);
		pthread_join(idle_loop_thread, NULL);
		printf("Idle thread cancelled\n");

		cleanup_idle();
		pthread_mutex_destroy(&idle_time_mutex);
#endif
	/*
	 * Note:
	 * If your plugin implements a settings dialog, it must be closed and deleted here, else the
	 * TeamSpeak client will most likely crash (DLL removed but dialog from DLL code still open).
	 */

	/* Free pluginID if we registered it */
	if(pluginID) {
		free(pluginID);
		pluginID = NULL;
	}
}

/****************************** Optional functions ********************************/
/*
 * Following functions are optional, if not needed you don't need to implement them.
 */

/* Tell client if plugin offers a configuration window. If this function is not implemented, it's an assumed "does not offer" (PLUGIN_OFFERS_NO_CONFIGURE). */
int ts3plugin_offersConfigure() {
	printf("PLUGIN: offersConfigure\n");
	/*
	 * Return values:
	 * PLUGIN_OFFERS_NO_CONFIGURE         - Plugin does not implement ts3plugin_configure
	 * PLUGIN_OFFERS_CONFIGURE_NEW_THREAD - Plugin does implement ts3plugin_configure and requests to run this function in an own thread
	 * PLUGIN_OFFERS_CONFIGURE_QT_THREAD  - Plugin does implement ts3plugin_configure and requests to run this function in the Qt GUI thread
	 */
	return PLUGIN_OFFERS_NO_CONFIGURE;  /* In this case ts3plugin_configure does not need to be implemented */
}

/* Plugin might offer a configuration window. If ts3plugin_offersConfigure returns 0, this function does not need to be implemented. */
void ts3plugin_configure(void* handle, void* qParentWidget) {
    printf("PLUGIN: configure\n");
}

/*
 * If the plugin wants to use error return codes, plugin commands, hotkeys or menu items, it needs to register a command ID. This function will be
 * automatically called after the plugin was initialized. This function is optional. If you don't use these features, this function can be omitted.
 * Note the passed pluginID parameter is no longer valid after calling this function, so you must copy it and store it in the plugin.
 */
void ts3plugin_registerPluginID(const char* id) {
	const size_t sz = strlen(id) + 1;
	pluginID = (char*)malloc(sz * sizeof(char));
	_strcpy(pluginID, sz, id);  /* The id buffer will invalidate after exiting this function */
	printf("PLUGIN: registerPluginID: %s\n", pluginID);
}

/* Plugin command keyword. Return NULL or "" if not used. */
const char* ts3plugin_commandKeyword() {
	return "afk";
}

/* Plugin processes console command. Return 0 if plugin handled the command, 1 if not handled. */
int ts3plugin_processCommand(uint64 serverConnectionHandlerID, const char* command) {
	char buf[COMMAND_BUFSIZE];
	char *s, *param1 = NULL, *param2 = NULL;
	int i = 0;
	enum { CMD_NONE = 0, CMD_IDLE_TIME, CMD_SET_AWAY} cmd = CMD_NONE;
#ifdef _WIN32
	char* context = NULL;
#endif

	printf("PLUGIN: process command: '%s'\n", command);

	_strcpy(buf, COMMAND_BUFSIZE, command);
#ifdef _WIN32
	s = strtok_s(buf, " ", &context);
#else
	s = strtok(buf, " ");
#endif
	while(s != NULL) {
		if(i == 0) {
			if(!strcmp(s, "idle_time")) {
				cmd = CMD_IDLE_TIME;
			} else if(!strcmp(s, "set_away")) {
				cmd = CMD_SET_AWAY;
			}
		} else if(i == 1) {
			param1 = s;
		} else {
			param2 = s;
		}
#ifdef _WIN32
		s = strtok_s(NULL, " ", &context);
#else
		s = strtok(NULL, " ");
#endif
		i++;
	}

	switch(cmd) {
		case CMD_NONE:
				help();
		case CMD_IDLE_TIME:  /* /test idle_time <seconds> */
			if(param1) {
				uint64 idle_time = (uint64)atoi(param1);
				if (idle_time < MIN_IDLE_TIME) {
					ts3Functions.printMessageToCurrentTab("idle_time below minimum threshold");
				} else {
#ifdef _WIN32

#else
					pthread_mutex_lock(&idle_time_mutex);
					max_idle_time = idle_time;
					pthread_mutex_unlock(&idle_time_mutex);
#endif
				}
			} else {
				char tbuf[COMMAND_BUFSIZE];
				sprintf_s(tbuf, "max idle time: %llu\n", max_idle_time);
				ts3Functions.printMessageToCurrentTab(tbuf);
			}
			break;
		case CMD_SET_AWAY:
			set_away_status(!get_away_status());
			break;
	}

	return 0;  /* Plugin handled command */
}

/* Required to release the memory for parameter "data" allocated in ts3plugin_infoData and ts3plugin_initMenus */
void ts3plugin_freeMemory(void* data) {
	free(data);
}

/*
 * Plugin requests to be always automatically loaded by the TeamSpeak 3 client unless
 * the user manually disabled it in the plugin dialog.
 * This function is optional. If missing, no autoload is assumed.
 */
int ts3plugin_requestAutoload() {
	return 1;  /* 1 = request autoloaded, 0 = do not request autoload */
}

int ts3plugin_onServerErrorEvent(uint64 serverConnectionHandlerID, const char* errorMessage, unsigned int error, const char* returnCode, const char* extraMessage) {
	printf("PLUGIN: onServerErrorEvent %llu %s %d %s\n", (long long unsigned int)serverConnectionHandlerID, errorMessage, error, (returnCode ? returnCode : ""));
	if(returnCode) {
		/* A plugin could now check the returnCode with previously (when calling a function) remembered returnCodes and react accordingly */
		/* In case of using a a plugin return code, the plugin can return:
		 * 0: Client will continue handling this error (print to chat tab)
		 * 1: Client will ignore this error, the plugin announces it has handled it */
		return 1;
	}
	return 0;  /* If no plugin return code was used, the return value of this function is ignored */
}

enum idle_status {
	ACTIVE = 0,
	IDLE
};

void *idle_loop(void* callback) {
		uint64 sleep_time = ACTIVITY_CHECK_RESOLUTION;
		uint64 idle_time;
		size_t away_status = get_away_status();
		for (;;) {
			idle_time = get_idle_time();
			printf("PLUGIN: current idle time: %llu\n", idle_time);

			if (idle_time > max_idle_time) { // If idle
				if (away_status == ACTIVE) {
					printf("PLUGIN: now idle\n");
					set_away_status(AWAY_ZZZ);
					away_status = IDLE;
				} //else {
					// still idle
				//}
			} else { // If not idle
				sleep_time += max_idle_time - idle_time;
				if (away_status == IDLE) {
						printf("PLUGIN: now active\n");
						set_away_status(AWAY_NONE);
						away_status = ACTIVE;
				} //else {
					// still active
				//}
			}
			printf("PLUGIN: sleeping for: %llu seconds\n", (sleep_time));
#ifdef _WIN32
			Sleep(sleep_time);
#else
			sleep(sleep_time);
#endif
			sleep_time = ACTIVITY_CHECK_RESOLUTION;
		}
}

int init_idle() {
#ifdef _WIN32

#else
	io_iterator_t iter;
	CFMutableDictionaryRef hid_match;

	IOMasterPort(MACH_PORT_NULL, &__idle_osx_master_port);

	hid_match = IOServiceMatching(kIOHIDSystemClass);
	IOServiceGetMatchingServices(__idle_osx_master_port, hid_match, &iter);
	if (iter == 0) {
		printf("Error getting kIOHIDSystemClass service");
	}

	__idle_osx_service = IOIteratorNext(iter);
  if (__idle_osx_service == 0) {
  	printf("Iterator's empty!\n");
	}

	IOObjectRelease(iter);
	return 0;
#endif
}

void cleanup_idle() {
#ifdef _WIN32

#else
	IOObjectRelease(__idle_osx_service);
#endif
}

uint64 get_idle_time() {
#ifdef _WIN32
	return 0;
#else
	CFMutableDictionaryRef properties = 0;
	CFTypeRef obj = NULL;
	uint64_t tHandle = 0;

	if(IORegistryEntryCreateCFProperties(__idle_osx_service, &properties, kCFAllocatorDefault, 0) == KERN_SUCCESS && properties != NULL) {
		obj = CFDictionaryGetValue(properties, CFSTR(kIOHIDIdleTimeKey));
		CFRetain(obj);
	} else {
		printf("Couldn't get system properties\n");
		return 0;
	}

	if (obj) {
		CFTypeID type = CFGetTypeID(obj);

		if (type == CFDataGetTypeID()) {
			CFDataGetBytes((CFDataRef)obj, CFRangeMake(0, sizeof(tHandle)), (UInt8*) &tHandle);
		} else if (type == CFNumberGetTypeID()) {
			CFNumberGetValue((CFNumberRef)obj, kCFNumberSInt64Type, &tHandle);
		} else {
			printf("%d: unsupported type\n", (int)type);
		}

		CFRelease(obj);

		// essentially divides by 10^9
		tHandle >>= 30;
	} else {
		printf("Can't find idle time\n");
	}

	CFRelease((CFTypeRef)properties);
	return tHandle;
#endif
}



int set_away_status(enum AwayStatus status) {
	uint64* servers = NULL;
	char* returnCode = NULL;

	ts3Functions.getServerConnectionHandlerList(&servers);
	for (int i=0; servers[i]; i++) {

		ts3Functions.setClientSelfVariableAsInt(servers[i], CLIENT_AWAY, status);
		if (status == AWAY_ZZZ) {
			// ts3Functions.setClientSelfVariableAsString(servers[i], CLIENT_AWAY_MESSAGE, "https://github.com/oopsies49/AFKPlugin");
		}
		ts3Functions.flushClientSelfUpdates(servers[i], returnCode);
	}

	return 0;
}

int get_away_status() {
	int result;
	uint64* servers = NULL;

	ts3Functions.getServerConnectionHandlerList(&servers);
	for (int i=0; servers[i]; i++) {
		ts3Functions.getClientSelfVariableAsInt(servers[i], CLIENT_AWAY, &result);

		if (result == AWAY_NONE) {
			return AWAY_NONE;
		}
	}

	return AWAY_ZZZ;
}

void help() {
	ts3Functions.printMessageToCurrentTab("AFKPlugin help:");
	ts3Functions.printMessageToCurrentTab("/afk idle_time [seconds] #gets or sets the max idle time parameter");
}
