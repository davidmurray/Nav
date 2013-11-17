#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <gps.h>
#include <curl/curl.h>

// [HACK] I need the actual header...
enum deg_str_type { deg_dd, deg_ddmm, deg_ddmmss };

static struct gps_data_t gpsdata;

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream);
static void get_google_maps_image_from_coordinates(double latitude, double longitude, int zoom_level, int width, int height, const char *out_file);
static void die(int reason);

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
	int written = fwrite(ptr, size, nmemb, (FILE *)stream);
	return written;
}

static void get_google_maps_image_from_coordinates(double latitude, double longitude, int zoom_level, int width, int height, const char *out_file)
{
	CURL *curl;
	FILE *file;

	curl_global_init(CURL_GLOBAL_ALL);

	/* Start the curl session. */
	curl = curl_easy_init();

	/* Possibly buffer overflow? */
	char URL[1000];
	sprintf(URL, "http://maps.googleapis.com/maps/api/staticmap?center=%f,%f&zoom=%d&size=%dx%d&maptype=map&sensor=true", latitude, longitude, zoom_level, width, height);

	/* Set the URL. */
	curl_easy_setopt(curl, CURLOPT_URL, URL);

	/* Tell curl to send data to the write_data function. */
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);

	file = fopen(out_file, "wb");
	if (file == NULL) {
			curl_easy_cleanup(curl);
			fprintf(stderr, "Couldn't open %s\n", out_file);

			die(-1);
	}

	/* Tell curl to write the data to the file instead of stdout. */
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

	/* Perform the request. */
	curl_easy_perform(curl);

	/* Close the file. */
	fclose(file);

	/* Cleanup. */
	curl_easy_cleanup(curl);
}

static void die(int reason)
{
	/* Tell gpsd to stop streaming data. */
	gps_stream(&gpsdata, WATCH_DISABLE, NULL);

	/* Close the connection to gpsd. */
	gps_close(&gpsdata);

	fprintf(stderr, "Exiting: %d\n", reason);

	exit(reason);
}

int main(int argc, char *argv[])
{
	/* Set up signal handlers. */
	signal(SIGINT, die);
	signal(SIGHUP, die);

	/* Open the stream to gpsd. */
	if (gps_open(NULL, NULL, &gpsdata) != 0) {
		fprintf(stderr,
			"err: no gpsd running or network error: %d, %s\n",
			 errno, gps_errstr(errno));
		exit(EXIT_FAILURE);
	}

	gps_stream(&gpsdata, WATCH_ENABLE, NULL);

	while (1) {
		if (!gps_waiting(&gpsdata, 500000)) {
			fprintf(stderr, "Timed out.\n");
				die(-1);
		} else {
				errno = 0;
				if (gps_read(&gpsdata) == -1) {
				fprintf(stderr, "Error: couldn't read.\n");
				die(-2);
				} else {
				int fix_mode = gpsdata.fix.mode;
				if (fix_mode == MODE_NO_FIX || fix_mode == MODE_NOT_SEEN) {
					fprintf(stderr, "No fix.\n");
				} else {
					double longitude = gpsdata.fix.longitude;
					double latitude = gpsdata.fix.latitude;

					fprintf(stdout, "Fix mode: %d\n\t Lat: %f %c\n\t Long: %f %c\n", fix_mode, latitude,
											  (latitude < 0) ? 'S' : 'N', longitude, (longitude < 0) ? 'W' : 'E');

					/* Get the Google Static map. */
					get_google_maps_image_from_coordinates(latitude, longitude, 17, 320, 240, "map.png");

					/* Not using this right now because it is broken. */
//					fprintf(stdout, "Fix mode: %d\n\t Lat: %s %c\n\t Long: %s %c\n", fix_mode, deg_to_str(deg_dd, fabs(latitude)),
//		       				(latitude < 0) ? 'S' : 'N', deg_to_str(deg_dd, fabs(longitude)), (longitude < 0) ? 'W' : 'E');
					}
			}
		}

		sleep(2);
	}

	return 0;
}
