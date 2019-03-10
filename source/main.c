#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#ifndef _WIN32
	#include <dirent.h>
	#include <pthread.h>
	#include <semaphore.h>
#endif

#include <curl/curl.h>

#ifdef _WIN32
	//this has to be AFTER curl
	#include <windows.h>
	#define strdup _strdup
#endif

#include "lib/cfgpath/cfgpath.h"
#include "lib/jsmn/jsmn.h"
#include "lib/libui/ui.h"
#include "lib/semver.c/semver.h"
#include "lib/zip/src/zip.h"

#define PROGRAM_NAME    "electron-shared"
#define PROGRAM_VERSION "0.1-alpha"

#if defined(WIN32) || defined(_WIN32)
	#define OS "win32"
	#define PATH_SEPARATOR "\\"
#elif defined(__APPLE__)
	#define OS "darwin"
	#define PATH_SEPARATOR "/"
#elif defined(__linux__)
	#define OS "linux"
	#define PATH_SEPARATOR "/"
#else
	#error OS not detected
#endif

#if defined(__i386__) || (defined(_WIN32) && !defined(_WIN64))
	#define ARCH "ia32"
#elif defined(__x86_64__) || defined(_WIN64)
	#define ARCH "x64"
#elif defined(__ARM_ARCH_7__)
	#define ARCH "armv7l"
#elif defined(__aarch64__)
	#define ARCH "arm64"
#else
	#error CPU architecture not detected
#endif

#define BUILDARCHSTRING OS "-" ARCH

#undef MIN
#undef MAX
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

void on_error(const char *message, ...);

unsigned long getTime() {
	struct timeval now;
	gettimeofday(&now,0);
	return now.tv_sec*1000 + now.tv_usec/1000.0;
}

#ifdef _WIN32
	HANDLE ui_thread;
	HANDLE _ui_mutex;
	HANDLE _ui_loaded;
#else
	pthread_t ui_thread;
	pthread_mutex_t _ui_mutex;
	sem_t _ui_loaded;
#endif

bool ui_enabled = false;

//for use in the window thread only
bool _ui_window_visible = false;
uiWindow *_ui_window = NULL;
uiLabel *_ui_label = NULL;
uiProgressBar *_ui_progress = NULL;

//mutexed
bool _ui_cancelled = false;

void _ui_on_cancel_clicked(uiButton *b, void *data) {
	void ui_cancel();

	ui_cancel();
}

int _ui_on_window_close(uiWindow *w, void *data) {
	void ui_cancel();

	ui_cancel();
	return 1;
}

static void *_ui_main(void *arg) {
	uiInitOptions options;
	memset(&options, 0, sizeof(uiInitOptions));

	const char *error = uiInit(&options);
	if(error){
		fprintf(stderr, "Unable to initialise libui\n");
		fprintf(stderr, "  %s\n", error);
		uiFreeInitError(error);
		#ifdef _WIN32
			ReleaseSemaphore(_ui_loaded, 1, NULL);
		#else
			sem_post(&_ui_loaded);
		#endif
		return NULL;
	}

	_ui_window = uiNewWindow("Updating Electron", 350, 50, false);
	uiWindowSetMargined(_ui_window, true);
	uiWindowOnClosing(_ui_window, _ui_on_window_close, NULL);
	// uiOnShouldQuit(onShouldQuit, _ui_window);

	uiBox *spacer;

	uiBox *rows = uiNewVerticalBox();
	uiBoxSetPadded(rows, true);
	uiWindowSetChild(_ui_window, uiControl(rows));

	spacer = uiNewVerticalBox();
	uiBoxAppend(rows, uiControl(spacer), true);

	_ui_label = uiNewLabel("");
	uiBoxAppend(rows, uiControl(_ui_label), false);

	_ui_progress = uiNewProgressBar();
	uiBoxAppend(rows, uiControl(_ui_progress), false);

	uiBox *actions = uiNewHorizontalBox();
	uiBoxAppend(rows, uiControl(actions), false);

	spacer = uiNewHorizontalBox();
	uiBoxAppend(actions, uiControl(spacer), true);

	uiButton *cancelButton = uiNewButton("Cancel");
	uiButtonOnClicked(cancelButton, _ui_on_cancel_clicked, NULL);
	uiBoxAppend(actions, uiControl(cancelButton), false);

	spacer = uiNewVerticalBox();
	uiBoxAppend(rows, uiControl(spacer), true);

	#ifdef _WIN32
		ReleaseSemaphore(_ui_loaded, 1, NULL);
	#else
		sem_post(&_ui_loaded);
	#endif

	uiMain();

	return NULL;
}

#ifdef _WIN32
DWORD WINAPI _ui_main_win32(LPVOID lpParam) {
	_ui_main(NULL);
	return 0;
}
#endif

typedef struct {
	const char *status;
	int progress;
} _Ui_update;

static void _ui_on_update(void *arg){
	_Ui_update *update = arg;

	if(update->status!=NULL){
		uiLabelSetText(_ui_label, update->status);
	}
	if(update->progress>=0){
		uiProgressBarSetValue(_ui_progress, update->progress);
	}
	free(update);

	//show the window the first time it is given information
	if(!_ui_window_visible){
		_ui_window_visible = true;
		uiControlShow(uiControl(_ui_window));
	}
}

static void _ui_on_error(void *arg){
	const char *message = arg;

	uiMsgBoxError(_ui_window, "Error updating Electron", message);

	#ifdef _WIN32
		ExitThread(0);
	#else
		pthread_exit(NULL);
	#endif
}

static void _ui_on_hide(void *arg){
	if(_ui_window_visible){
		_ui_window_visible = false;
		uiControlHide(uiControl(_ui_window));
	}
}

void ui_init() {
	if(ui_enabled) return;

	_ui_window_visible = false; //reset this before it belongs to the thread

	#ifdef _WIN32
		_ui_mutex = CreateMutex(NULL, FALSE, NULL);
		_ui_loaded = CreateSemaphore(NULL, 0, 1, NULL);

		ui_thread = CreateThread(NULL, 0, _ui_main_win32, NULL, 0, NULL);
		if(!ui_thread){
			fprintf(stderr, "Error creating ui thread\n");
			return;
		}

		WaitForSingleObject(_ui_loaded, INFINITE);

	#else
		pthread_mutex_init(&_ui_mutex, NULL);
		sem_init(&_ui_loaded, 0, 0);

		if(pthread_create(&ui_thread, NULL, _ui_main, NULL)) {
			fprintf(stderr, "Error creating ui thread\n");
			return;
		}

		sem_wait(&_ui_loaded);
	#endif

	ui_enabled = true;
}

void ui_hide() {
	if(!ui_enabled) return;

	uiQueueMain(_ui_on_hide, NULL);
}

void ui_status(const char *status) {
	if(!ui_enabled) return;

	_Ui_update *update = malloc(sizeof(*update));
	update->status = status;
	update->progress = -1;

	uiQueueMain(_ui_on_update, update);
}

void ui_progress(int progress) {
	if(!ui_enabled) return;

	_Ui_update *update = malloc(sizeof(*update));
	update->status = NULL;
	update->progress = progress;

	uiQueueMain(_ui_on_update, update);
}

void ui_cancel() {
	#ifdef _WIN32
		WaitForSingleObject(_ui_mutex, INFINITE);
			_ui_cancelled = true;
		ReleaseMutex(_ui_mutex);
	#else
		pthread_mutex_lock(&_ui_mutex);
			_ui_cancelled = true;
		pthread_mutex_unlock(&_ui_mutex);
	#endif
}

bool ui_is_cancelled() {
	bool result;
	#ifdef _WIN32
		WaitForSingleObject(_ui_mutex, INFINITE);
			result = _ui_cancelled;
		ReleaseMutex(_ui_mutex);
	#else
		pthread_mutex_lock(&_ui_mutex);
			result = _ui_cancelled;
		pthread_mutex_unlock(&_ui_mutex);
	#endif
	return result;
}

void ui_error(const char *message) {
	if(!ui_enabled) return;

	uiQueueMain(_ui_on_error, (void*)message);
	#ifdef _WIN32
		WaitForSingleObject(ui_thread, INFINITE);
	#else
		pthread_join(ui_thread, NULL);
	#endif

	ui_enabled = false;
}

void print_help(const char *name){
	printf("Usage: %s [options] [path]\n", name);
	printf("       %s [command]\n", name);
	printf("\n");
	printf("  Download the latest supporting version of Electron for the specified PATH\n");
	printf("  and then run the application using it.\n");
	printf("\n");
	printf("  If omitted, path will default to \"app\"\n");
	printf("\n");
	printf("  PATH must be a directory containing a project.json file or the path to an\n");
	printf("  .asar archive (extension optional)\n");
	printf("\n");
	printf("  Options:\n");
	printf("    -d, --downloadOnly   Download any required Electron updates and then exit\n");
	printf("    -n, --noDownload     Do NOT download if required Electron is not available\n");
	printf("    -s, --silent         Do not display any user feedback while downloading\n");
	printf("\n");
	printf("    Any other options will be passed directly to Electron (if it is executed)\n");
	printf("\n");
	printf("  Commands:\n");
	printf("    -h, --help           Display this help and exit\n");
	printf("    -l, --list           Print the currently downloaded Electron versions\n");
	printf("    -v, --version        Output version information and exit\n");
}

void print_version(){
	printf(PROGRAM_NAME " " PROGRAM_VERSION "\n");
}

void print_downloads(){
	char path[MAX_PATH];
	get_user_cache_folder(path, MAX_PATH, PROGRAM_NAME);

	int count = 0;

	#ifdef _WIN32
		WIN32_FIND_DATA findData;

		char *searchpath = malloc(strlen(path)+2);
		strcpy(searchpath, path);
		strcat(searchpath, "*"); //put a wildcard star on the end
		HANDLE search = FindFirstFile(searchpath, &findData);
			if(search==INVALID_HANDLE_VALUE && GetLastError()!=ERROR_FILE_NOT_FOUND){
				fprintf(stderr, "Unable to access path: %s\n", path);
				return;
			}

			printf("Currently downloaded Electron runtimes (%s):\n", path);

			if(search!=INVALID_HANDLE_VALUE){
				do{
					if(findData.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY && findData.cFileName[0]!='.'){
						count++;
						printf("  %s\n", findData.cFileName);
					}
				}while(FindNextFile(search, &findData));
			}
		FindClose(search);
		free(searchpath);

	#else
		DIR *dir = opendir(path);
			if(!dir){
				fprintf(stderr, "Unable to access path: %s\n", path);
				return;
			}

			printf("Currently downloaded Electron runtimes (%s):\n", path);

			struct dirent *entry;
			while(entry = readdir(dir)){
				if(entry->d_type==DT_DIR && entry->d_name[0]!='.'){
					count++;
					printf("  %s\n", entry->d_name);
				}
			}
		closedir(dir);
	#endif

	if(!count){
		printf("  none found\n");
	}
}

int json_init(jsmn_parser *jsonParser, jsmntok_t *json[], char *data);
bool json_find(jsmntok_t json[], int jsonLength, char *data, int parent, int *_position, const char *name, jsmntype_t type);
void json_next(jsmntok_t json[], int jsonLength, int *position);

int read_file_fs(const char *directory, const char *filename, char **buffer) {
	*buffer = 0;

	char *filePath = malloc(strlen(directory)+strlen(filename)+1);
	sprintf(filePath, "%s" PATH_SEPARATOR "%s", directory, filename);

	FILE *file;
	size_t size;
	size_t read;

	file = fopen(filePath, "r");
	if(!file){
		free(filePath);
		return errno?errno:-1;
	}

	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);

	*buffer = malloc(size);
	if(!buffer){
		fclose(file);
		free(filePath);
		return errno?errno:-1;
	}

	read = fread(*buffer, 1, size, file);
	fclose(file);

	if(read<size){
		free(*buffer);
		*buffer = 0;
	}

	free(filePath);

	return 0;
}

#define ASAR_ALIGN(x) (((x)+7)/8*8)

// only reads toplevel files for now, cos that's all we need
int read_file_asar(const char *archive, const char *filename, char **buffer) {
	*buffer = NULL;

	FILE *file;
	file = fopen(archive, "rb");
	if(!file){
		return errno?errno:-1;
	}

	int error = -1;

	char *header = NULL;
	jsmn_parser jsonParser;
	jsmntok_t *json = NULL;

	do{
		uint32_t headerSize = 0;

		if(fseek(file, 4, SEEK_CUR)) break;
		if(!fread(&headerSize, 4, 1, file)) break;

		uint32_t headerStringSize = 0;
		if(fseek(file, 4, SEEK_CUR)) break;
		if(!fread(&headerStringSize, 4, 1, file)) break;

		header = malloc(headerStringSize);
		if(!fread(header, headerStringSize, 1, file)) break;

		// if(fseek(file, ASAR_ALIGN(16+headerStringSize), SEEK_SET)) break; //apparently these AREN'T aligned?


		int parsed = json_init(&jsonParser, &json, header);
		if(!json) break;

		if(parsed<1||json[0].type!=JSMN_OBJECT){
			fprintf(stderr, "Error parsing ASAR header JSON\n");
			break;
		}

		int filesPosition = 1;
		if(!json_find(json, parsed, header, 0, &filesPosition, "files", JSMN_OBJECT)) break;

		int filePosition = filesPosition+1;
		if(!json_find(json, parsed, header, filesPosition, &filePosition, filename, JSMN_OBJECT)) break;

		int sizePosition = filePosition+1;
		int offsetPosition = filePosition+1;

		if(
			!json_find(json, parsed, header, filePosition, &sizePosition, "size", JSMN_STRING)||
			!json_find(json, parsed, header, filePosition, &offsetPosition, "offset", JSMN_STRING)
		) break;

		unsigned long int size = strtoul(&header[json[sizePosition].start], NULL, 10);
		unsigned long int offset = strtoul(&header[json[offsetPosition].start], NULL, 10);

		*buffer = malloc(size);
		if(!*buffer){
			fprintf(stderr, "Out of memory reading \"%s\" from \"%s\" ASAR archive\n", filename, archive);
			break;
		}

		if(
			fseek(file, offset, SEEK_CUR)||
			fread(*buffer, 1, size, file)!=size
		){
			free(*buffer);
			*buffer = NULL;
			break;
		}

		error = 0;

	}while(false);

	free(header);
	free(json);

	fclose(file);

	return error;
}

int read_file(const char *directory, const char *filename, char **buffer) {
	int error = read_file_fs(directory, filename, buffer);
	if(error==ENOTDIR){
		error = read_file_asar(directory, filename, buffer);
	}

	return error;
}


bool extract_files(const char *archive, const char *path) {
	ui_status("Extracting...");

	bool success = zip_extract(archive, path, NULL, NULL) == 0;

	return success;
}

typedef struct {
	char *buffer;
	size_t length;
	size_t size;
} _Curl_buffer;

static size_t _on_curl_write_memory(const char *ptr, size_t size, size_t nmemb, void *userdata) {
	_Curl_buffer *buffer = userdata;

	size_t chunkSize = size*nmemb;

	if(buffer->size+chunkSize>=buffer->size){
		buffer->size = buffer->size+chunkSize+(64*1024);

		char *newBuffer = realloc(buffer->buffer, buffer->size);
		if(!newBuffer){
			fprintf(stderr, "Out of memory making web request\n");
			return 0;
		}

		buffer->buffer = newBuffer;
	}

	memcpy(&buffer->buffer[buffer->length], ptr, chunkSize);

	buffer->length += chunkSize;
	buffer->buffer[buffer->length] = '\0';

	return chunkSize;
}

static size_t _on_curl_write_file(const char *ptr, size_t size, size_t nmemb, void *userdata) {
	FILE *file = userdata;

	return fwrite(ptr, size, nmemb, file);
}

static int _on_curl_progress(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
	static unsigned long lastUiTime = 0;

	unsigned long now = getTime();

	if(now-lastUiTime>1000/30){
		lastUiTime = now;

		if(dltotal>0){
			ui_progress((int)((dlnow*100)/dltotal));
		}else{
			ui_progress(0);
		}
	}

	if(ui_is_cancelled()) return 1;

	return 0;
}

void fetch(char **buffer, const char *url) {
	ui_status("Fetching update list...");

	*buffer = 0;

	CURL *curl;
	CURLcode res;
 
	curl = curl_easy_init();
	if(!curl){
		on_error("Error initialising libcurl");
		return;
	}

	_Curl_buffer curlBuffer = {
		.buffer = malloc(4096),
		.length = 0,
		.size = 4096
	};

	curlBuffer.buffer[0] = '\0';

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, PROGRAM_NAME "/" PROGRAM_VERSION);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _on_curl_write_memory);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &curlBuffer);
	curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, _on_curl_progress);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, false);

	CURLcode response = curl_easy_perform(curl);

	if(response!=CURLE_OK){
		if(response!=CURLE_ABORTED_BY_CALLBACK){
			switch(response){
				case CURLE_COULDNT_CONNECT:
				case CURLE_COULDNT_RESOLVE_HOST:
					on_error("Could not connect (%i)\nPlease ensure you have access to the internet", response);
				break;
				default:
					on_error("Error retrieving %s\n%s", url, curl_easy_strerror(response));
			}
		}

		free(curlBuffer.buffer);
		curlBuffer.buffer = NULL;
	}

	*buffer = curlBuffer.buffer;

	curl_easy_cleanup(curl);
}

bool download(const char *url, const char *filename) {
	ui_status("Downloading...");

	CURL *curl;
	CURLcode res;
 
	curl = curl_easy_init();
	if(!curl){
		on_error("Error initialising libcurl");
		return false;
	}

	FILE *file = fopen(filename, "wb");
	if(!file){
		on_error("Unable to write to \"%s\"", filename);
		return false;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, PROGRAM_NAME "/" PROGRAM_VERSION);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _on_curl_write_file);
	if(ui_enabled){
		curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, _on_curl_progress);
	}
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, false);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

	CURLcode response = curl_easy_perform(curl);

	bool success = true;

	if(response!=CURLE_OK){
		if(response!=CURLE_ABORTED_BY_CALLBACK){
			on_error("Error downloading \"%s\"\n  %s", url, curl_easy_strerror(response));
		}

		success = false;
	}

	curl_easy_cleanup(curl);
	fclose(file);

	return success;
}

int json_init(jsmn_parser *jsonParser, jsmntok_t *json[], char *data) {
	size_t dataLength = strlen(data);

	jsmn_init(jsonParser);
	int size = jsmn_parse(jsonParser, data, dataLength, NULL, 0);
	if(size<=0){
		*json = NULL;
		return 0;
	}

	*json = malloc(size*sizeof(jsmntok_t));
	if(!json){
		fprintf(stderr, "Out of memory parsing JSON\n");
		free(*json);
		*json = NULL;
		return 0;
	}

	jsmn_init(jsonParser);
	return jsmn_parse(jsonParser, data, dataLength, *json, size);
}

bool json_find(jsmntok_t json[], int jsonLength, char *data, int parent, int *_position, const char *name, jsmntype_t type) {
	int position = *_position;
	int maxByte = json[parent].end;
	int nameLength = strlen(name);

	while(position<jsonLength&&json[position].start<maxByte){
		if(json[position].end-json[position].start==nameLength && !strncmp(name, &data[json[position].start], nameLength)){
			++position;

			if(json[position].type==type || type==JSMN_STRING && json[position].type==JSMN_PRIMITIVE){
				*_position = position;
				return true;
			}else{
				return false;
			}
		}

		json_next(json, jsonLength, &position);
	}

	return false;
}

void json_next(jsmntok_t json[], int jsonLength, int *position) {
	// if(json[*position].start==json[*position].end){
	// 	(*position)++;
	// 	return;
	// }
	for(int skipTo=json[*position].end; *position<jsonLength&&json[*position].start<=skipTo; (*position)++);
}

void read_electron_requirement(char **requirement, char *data) {
	jsmn_parser jsonParser;
	jsmntok_t *json;

	*requirement = NULL;

	int parsed = json_init(&jsonParser, &json, data);

	if(!json) return;

	if(parsed<1||json[0].type!=JSMN_OBJECT){
		fprintf(stderr, "Error parsing package.json\n");
		free(json);
		return;
	}

	int depPosition = 1;
	if(json_find(json, parsed, data, 0, &depPosition, "dependencies", JSMN_OBJECT)){

		int electronPosition = depPosition+1;
		if(json_find(json, parsed, data, depPosition, &electronPosition, "electron-prebuilt", JSMN_STRING)){
			*requirement = &data[json[electronPosition].start];
			data[json[electronPosition].end] = '\0';
		}

		electronPosition = depPosition+1;
		if(json_find(json, parsed, data, depPosition, &electronPosition, "electron", JSMN_STRING)){
			*requirement = &data[json[electronPosition].start];
			data[json[electronPosition].end] = '\0';
		}
	}

	depPosition = 1;
	if(json_find(json, parsed, data, 0, &depPosition, "devDependencies", JSMN_OBJECT)){

		int electronPosition = depPosition+1;
		if(json_find(json, parsed, data, depPosition, &electronPosition, "electron-prebuilt", JSMN_STRING)){
			*requirement = &data[json[electronPosition].start];
			data[json[electronPosition].end] = '\0';
		}

		electronPosition = depPosition+1;
		if(json_find(json, parsed, data, depPosition, &electronPosition, "electron", JSMN_STRING)){
			*requirement = &data[json[electronPosition].start];
			data[json[electronPosition].end] = '\0';
		}
	}

	if(*requirement){
		*requirement = strdup(*requirement);
	}

	free(json);
}

void on_error(const char *message, ...) {
	static char buffer[512];
	va_list args;
	va_start(args, message);
	vsnprintf(buffer, sizeof(buffer)/sizeof(buffer[0]), message, args);
	va_end(args);

	fprintf(stderr, "%s\n", buffer);
	ui_error(buffer);
}

#ifdef _WIN32

	// Windows doesn't support passing multiple parameters, and instead condenses them to a single commandline string
	// Thus we must build this string ourselves, quote all arguments, and escape special characters
	int execvp_win32(char *file, char *const argv[], int *launchError) {
		char *commandline = malloc(1);
		commandline[0] = '\0';
		size_t commandlineLength = 0;

		*launchError = 0;

		int i = 0;
		for(char *arg=argv[i]; arg; arg=argv[++i]){
			if(arg[0]=='\0') continue;

			commandline = realloc(commandline, commandlineLength+1+1+strlen(arg)*2+1+1); //allocate room for a space, open quote, double the size the string (escaping every char), a close quote, and a null

			if(commandlineLength>0){
				commandline[commandlineLength++] = ' ';
			}

			commandline[commandlineLength++] = '"';

			for(;*arg;arg++){
				unsigned escapes = 0;
				while(*arg=='\\'){
					commandline[commandlineLength++] = *arg;
					escapes++;
					arg++;
				}

				switch(*arg){
					case '\0':
						//if string is ending, escape all escape characters so the closing " can be parsed
						for(int i2=0;i2<escapes;i2++){
							commandline[commandlineLength++] = '\\';
						}
					break;
					case '&':
					case '\\':
					case '<':
					case '>':
					case '^':
					case '|':
					case '"':
						//also double escape if a special character follows, followed by that character, escaped
						for(int i2=0;i2<escapes;i2++){
							commandline[commandlineLength++] = '\\';
						}
						commandline[commandlineLength++] = '\\';
						commandline[commandlineLength++] = *arg;
					break;
					default:
						commandline[commandlineLength++] = *arg;
				}
			}

			commandline[commandlineLength++] = '"';
			commandline[commandlineLength] = '\0';
		}

		STARTUPINFO startupInfo;
		memset(&startupInfo, 0, sizeof(startupInfo));
		startupInfo.cb = sizeof(startupInfo);

		PROCESS_INFORMATION processInfo;
		memset(&processInfo, 0, sizeof(processInfo));

		if(!CreateProcess(file, commandline, NULL, NULL, FALSE, CREATE_DEFAULT_ERROR_MODE, NULL, NULL, &startupInfo, &processInfo)){
			*launchError = GetLastError();
			free(commandline);
			return 1;
		}

		int result = WaitForSingleObject(processInfo.hProcess, INFINITE);

		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);

		free(commandline);

		return result;
	}
#endif

int main(int argc, const char *argv[]) {
	char *projectPath = "app";
	bool projectPathSpecified = false;
	bool noDownload = false;
	bool downloadOnly = false;
	bool silent = false;

	char **electronParams = malloc((argc+1)*sizeof(const char*)); // +1 in case a project path wasn't included and we append one
	int electronParamCount = 2; //we'll leave room for the electron path and the project path, which will be param 1

	#ifdef _WIN32
		if(AttachConsole(ATTACH_PARENT_PROCESS)||AttachConsole(GetCurrentProcessId())){
			freopen("CONIN$", "r", stdin);
			freopen("CONOUT$", "w", stdout);
			freopen("CONOUT$", "w", stderr);
		}
	#endif

	{ //read params

		for(int i=1; i<argc; i++){
			const char *arg = argv[i];
			if(arg[0]=='\0') continue;

			if(!projectPathSpecified && arg[0]!='-'){
				projectPathSpecified = true;
				projectPath = strdup(arg);

				{ //strip trailing slashes
					int length = strlen(projectPath);
					while(length>0&&(projectPath[length-1]=='/'||projectPath[length-1]=='\\')){
						projectPath[--length]='\0';
					}
				}

				continue;

			}else if(!strcmp(arg,"-h")||!strcmp(arg,"--help")){
				print_help(argv[0]);
				return 0;

			}else if(!strcmp(arg,"-l")||!strcmp(arg,"--list")){
				print_downloads();
				return 0;

			}else if(!strcmp(arg,"-v")||!strcmp(arg,"--version")){
				print_version();
				return 0;

			}else if(!strcmp(arg,"-n")||!strcmp(arg,"--noDownload")){
				noDownload = true;
				continue;

			}else if(!strcmp(arg,"-d")||!strcmp(arg,"--downloadOnly")){
				downloadOnly = true;
				continue;

			}else if(!strcmp(arg,"-s")||!strcmp(arg,"--silent")){
				silent = true;
				continue;
			}

			electronParams[electronParamCount++] = strdup(arg);
		}
		electronParams[electronParamCount] = NULL;
	}

	char *electronRequirement;

	{ //read project file

		char *projectFile;

		{
			int error = read_file(projectPath, "package.json", &projectFile);

			if(error==ENOENT){
				char *projectPathExtended = malloc(strlen(projectPath)+5+1);
				strcpy(projectPathExtended, projectPath);
				strcat(projectPathExtended, ".asar");

				error = read_file(projectPathExtended, "package.json", &projectFile);
				if(error!=ENOENT){
					projectPath = projectPathExtended;
				}
			}

			if(error){
				if(error==ENOENT){
					fprintf(stderr, "File not found: %s" PATH_SEPARATOR "package.json\n", projectPath);
				}else{
					fprintf(stderr, "Unable to access: %s" PATH_SEPARATOR "package.json\n", projectPath);
				}

				printf("\n");
				print_help(argv[0]);

				return 1;
			}
		}

		read_electron_requirement(&electronRequirement, projectFile);

		if(!electronRequirement){
			fprintf(stderr, "Unable to read an \"electron\" dependency line in package.json\n");
			return 1;
		}

		free(projectFile);
	}

	//FIXME: we only support a single operator and semver requirement for now ("~x.x.x" etc). We're meant to support sets, too (like "1.2.7 || >=1.2.9 <2.0.0"). Just.. that's more work
	semver_t versionRequirement;
	char versionOp[3] = "=\0";

	{
		char *semverString = electronRequirement;

		{ //read first 2 symbols (non-numbers) as operator
			if(*semverString && (*semverString<='0'||*semverString>='9')){
				versionOp[0] = *semverString;
				semverString++;

				if(*semverString && (*semverString<='0'||*semverString>='9')){
					versionOp[1] = *semverString;
					semverString++;
				}
			}
		}

		{ //strip off *all* additional "=" or "v" as per npm-semver
			while(*semverString=='='||*semverString=='v'){
				semverString++;
			}
		}

		if(semver_parse(semverString, &versionRequirement)){
			fprintf(stderr, "Unable to parse Electron dependency version \"%s\"\n", electronRequirement);
			return 1;
		}
	}

	char storePath[MAX_PATH];
	get_user_cache_folder(storePath, MAX_PATH, PROGRAM_NAME);

	semver_t bestVersion;
	char *bestVersionString = NULL;
	{
	#ifdef _WIN32
		WIN32_FIND_DATA findData;

		char *storePathSearch = malloc(strlen(storePath)+2);
		strcpy(storePathSearch, storePath);
		strcat(storePathSearch, "*"); //put a wildcard star on the end

		HANDLE search = FindFirstFile(storePathSearch, &findData);
			free(storePathSearch);

			if(search==INVALID_HANDLE_VALUE && GetLastError()!=ERROR_FILE_NOT_FOUND){
				on_error("Unable to access path: %s", storePath);
				return 1;
			}

			if(search!=INVALID_HANDLE_VALUE){
				do{
					if(findData.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY && findData.cFileName[0]!='.'){
						semver_t version;

						if(!semver_parse(findData.cFileName, &version)){
							if(semver_satisfies(version, versionRequirement, versionOp) && (!bestVersionString || semver_compare(version, bestVersion)>0)){
								bestVersion = version;
								free(bestVersionString);
								bestVersionString = strdup(findData.cFileName);
							}
						}
					}
				}while(FindNextFile(search, &findData));
			}
		FindClose(search);

	#else
		DIR *dir = opendir(storePath);
			if(!dir){
				on_error("Unable to access path: %s", storePath);
				return 1;
			}

			struct dirent *entry;
			while(entry = readdir(dir)){
				if(entry->d_type==DT_DIR && entry->d_name[0]!='.'){
					semver_t version;

					if(!semver_parse(entry->d_name, &version)){
						if(semver_satisfies(version, versionRequirement, versionOp) && (!bestVersionString || semver_compare(version, bestVersion)>0)){
							bestVersion = version;
							free(bestVersionString);
							bestVersionString = strdup(entry->d_name);
						}
					}
				}
			}
		closedir(dir);
	#endif
	}

	if(!bestVersionString){
		printf("This application requires Electron %s\n", electronRequirement);

		if(noDownload){
			fprintf(stderr, "A compatible version is not currently downloaded\n");
			return 1;
		}

		if(!silent){
			ui_init();
		}

		printf("Fetching release list from GitHub...\n");

		const char *apiUrl = "https://api.github.com/repos/electron/electron/releases?per_page=100"; //FIXME: only pulls the last 100 releases. Really we should be paginating this list
		char *api;
		fetch(&api, apiUrl);

		if(ui_is_cancelled()) return 0;

		if(!api){
			on_error("Unable to retrieve Electron download list");
			return 1;
		}

		jsmn_parser jsonParser;
		jsmntok_t *json;

		int parsed = json_init(&jsonParser, &json, api);

		if(!json){
			return 1;
		}

		if(parsed<1){
			on_error("Error parsing response from GitHub API (response was not valid JSON)");
			return 1;
		}

		if(json[0].type!=JSMN_ARRAY){
			on_error("Error parsing response from GitHub API");
			return 1;
		}

		char *bestVersionUrl = NULL;

		{
			semver_t bestVersion;

			int position = 1;

			char *endName = "-" BUILDARCHSTRING ".zip";
			int endNameLength = strlen(endName);

			while(position<parsed){
				if(json[position].type!=JSMN_OBJECT){
					json_next(json, parsed, &position);
					continue;
				}

				int releasePosition = position;

				int tagPosition = releasePosition+1;
				if(json_find(json, parsed, api, releasePosition, &tagPosition, "tag_name", JSMN_STRING)){
					char *tag = &api[json[tagPosition].start];
					api[json[tagPosition].end] = '\0';

					int assetsPosition = releasePosition+1;
					if(json_find(json, parsed, api, releasePosition, &assetsPosition, "assets", JSMN_ARRAY)){

						int afterAssets = assetsPosition;
						json_next(json, parsed, &afterAssets);

						for(int assetPosition=assetsPosition+1; assetPosition<afterAssets; json_next(json, parsed, &assetPosition)){
							int namePosition = assetPosition+1;
							int typePosition = assetPosition+1;
							int urlPosition = assetPosition+1;
							if( true
								&& json_find(json, parsed, api, assetPosition, &namePosition, "name", JSMN_STRING)
									&& json[namePosition].end-json[namePosition].start > 9+endNameLength
									&& !strncmp("electron-", &api[json[namePosition].start], 9)
									&& !strncmp(endName, &api[json[namePosition].end-endNameLength], endNameLength)
								&& json_find(json, parsed, api, assetPosition, &typePosition, "content_type", JSMN_STRING)
									&& json[typePosition].end-json[typePosition].start == 15
									&& !strncmp("application/zip", &api[json[typePosition].start], 15)
								&& json_find(json, parsed, api, assetPosition, &urlPosition, "browser_download_url", JSMN_STRING)
							){
								char *versionString = &api[json[namePosition].start+9];
								api[json[namePosition].end-endNameLength] = '\0';
								while(versionString[0]=='v')versionString++;

								char *url = &api[json[urlPosition].start];
								api[json[urlPosition].end] = '\0';

								semver_t version;
								if(!semver_parse(versionString, &version)){
									if(semver_satisfies(version, versionRequirement, versionOp) && (!bestVersionString || semver_compare(version, bestVersion)>0)){
										bestVersion = version;
										free(bestVersionString);
										bestVersionString = strdup(versionString);
										bestVersionUrl = strdup(url);
									}
								}
							}
						}
					}
				}

				json_next(json, parsed, &position);
			}
		}

		free(json);

		if(!bestVersionUrl){
			on_error("Unable to find a compatible version of Electron for download");
			return 1;
		}

		printf("Downloading Electron %s...\n", bestVersionString);

		{
			char *destinationFilename = malloc(strlen(bestVersionString)+4+1);
			strcpy(destinationFilename, bestVersionString);
			strcat(destinationFilename, ".zip");

			char *downloadDestination = malloc(strlen(storePath)+strlen(destinationFilename)+1);
			strcpy(downloadDestination, storePath);
			strcat(downloadDestination, destinationFilename);

			char *extractDestination = malloc(strlen(storePath)+strlen(bestVersionString)+1);
			strcpy(extractDestination, storePath);
			strcat(extractDestination, bestVersionString);

			if(!download(bestVersionUrl, downloadDestination)){
				if(ui_is_cancelled()) return 0;

				return 1;
			}

			printf("Extracting...\n");

			#ifdef _WIN32
				if(mkdir(extractDestination)<0){
					on_error("Unable to create path for writing: %s", extractDestination);
				}
			#else
				if(mkdir(extractDestination, 0700)<0){
					on_error("Unable to create path for writing: %s", extractDestination);
				}
			#endif

			if(!extract_files(downloadDestination, extractDestination)){
				rmdir(extractDestination);

				if(ui_is_cancelled()) return 0;

				on_error("An error occurred extracting the downloaded Electron archive");
				return 1;
			}

			remove(downloadDestination);
		}
	}

	if(!downloadOnly){
		printf("Launching Electron %s (%s)...\n", bestVersionString, electronRequirement);

		#ifdef _WIN32
			char *electronPath = malloc(strlen(storePath)+strlen(bestVersionString)+12+1);
			sprintf(electronPath, "%s%s" PATH_SEPARATOR "electron.exe", storePath, bestVersionString);
		#else
			char *electronPath = malloc(strlen(storePath)+strlen(bestVersionString)+8+1);
			sprintf(electronPath, "%s%s" PATH_SEPARATOR "electron", storePath, bestVersionString);
		#endif

		electronParams[0] = electronPath;
		electronParams[1] = projectPath;
		electronParams[electronParamCount] = NULL;

		#ifdef _WIN32
			ui_hide();

			int launchError;
			int result = execvp_win32(electronPath, electronParams, &launchError);

			if(launchError){
				if(!silent&&!ui_enabled){
					ui_init();
				}
				on_error("Error %i launching %s", launchError, electronPath);
				result = 1;
			}

			return result;
		#else
			return execvp(electronPath, electronParams);
		#endif
	}

	return 0;
}
