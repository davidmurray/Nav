#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <gps.h>
#include <curl/curl.h>

// [HACK] I need the actual header...
enum deg_str_type { deg_dd, deg_ddmm, deg_ddmmss };

static struct gps_data_t gpsdata;
static FILE *log_file;

static void get_google_maps_image_from_coordinates(double latitude, double longitude, int zoom_level, int width, int height, const char *out_file);
static void die(int reason);

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

	/* Close the log file. */
	fclose(log_file);

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

	/* Create an appropriate file name using from the current date & time. */
	time_t timestamp = time(NULL);
	struct tm local_time = *localtime(&timestamp);

	char path[500];
	sprintf(path, "log-%d-%d-%d-%d:%d:%d.txt", local_time.tm_year + 1900,
						local_time.tm_mon + 1,
						local_time.tm_mday,
						local_time.tm_hour,
						local_time.tm_min,
						local_time.tm_sec);
	/* Open the log file. */
	log_file = fopen(path, "w");
	if (!log_file) {
		fprintf(stderr, "Couldn't open file at path: %s", path);
		die(-1);
	}

	/* Write initial information in the log. */
	fprintf(log_file, "// Log format: longitude, latitude, altitude, speed, climb\n");

	while (1) {
		if (!gps_waiting(&gpsdata, 500000)) {
			fprintf(stderr, "Timed out.\n");
				die(-1);
		} else {
			if (gps_read(&gpsdata) == -1) {
				fprintf(stderr, "Error: couldn't read.\n");
				die(-2);
			} else {
				int fix_mode = gpsdata.fix.mode;
				if (fix_mode == MODE_NO_FIX || fix_mode == MODE_NOT_SEEN) {
					fprintf(stderr, "No fix.\n");
				} else {
					double latitude = gpsdata.fix.latitude;
					double longitude = gpsdata.fix.longitude;

					fprintf(stdout, "Fix mode: %d\n\t Lat: %f %c\n\t Long: %f %c\n", fix_mode, latitude,
											  (latitude < 0) ? 'S' : 'N', longitude, (longitude < 0) ? 'W' : 'E');

					/* Get the Google Static map. */
					get_google_maps_image_from_coordinates(latitude, longitude, 17, 320, 240, "map.png");

					/*
					 * Write data to the log file.
					 * The idiots at Google thought that being hipsters was a good idea and asking for "longitude, latitude" was better
					 * than using the widely accepted "latitude, longitude" format. Fuckers.
					 */
					fprintf(log_file, "%f,%f,%f,%f,%f\n", longitude, latitude, gpsdata.fix.altitude, gpsdata.fix.speed, gpsdata.fix.climb);

					/* Not using this right now because it is broken. */
//					fprintf(stdout, "Fix mode: %d\n\t Lat: %s %c\n\t Long: %s %c\n", fix_mode, deg_to_str(deg_dd, fabs(latitude)),
//		       				(latitude < 0) ? 'S' : 'N', deg_to_str(deg_dd, fabs(longitude)), (longitude < 0) ? 'W' : 'E');
				}
			}
		}

		sleep(1);
	}

	return 0;
}
