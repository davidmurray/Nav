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
#include "wand/MagickWand.h"
#include "LodePNG/lodepng.h"

// TODO
// make it so that failing something doesn't quit the whole process.

// [HACK] I need the actual header...
enum deg_str_type { deg_dd, deg_ddmm, deg_ddmmss };

static struct gps_data_t gpsdata;
static FILE *log_file;
static MagickWand *wand;
static DrawingWand *drawing_wand;
static PixelWand *pixel_wand;

static int blit_png_at_path_to_framebuffer(char *path, char *framebuffer);
static int draw_error_image_with_text(char *text, char *file_path);
static int get_google_maps_image_from_coordinates(double latitude, double longitude, int zoom_level, int width, int height, const char *file_path);
static void signal_handler(int signal);
static void die(int reason);

static int blit_png_at_path_to_framebuffer(char *path, char *framebuffer)
{
	unsigned width, height, x, y;
	unsigned char *image;
	int ret = 0;
	FILE *fb;

	fb = fopen(framebuffer, "w");
	if (!fb) {
		fprintf(stderr, "Couldn't open %s.", framebuffer);
		ret = -2;
		goto out;
	}

	if (lodepng_decode_file(&image, &width, &height, path, LCT_RGB, 16) > 0) {
		fprintf(stderr, "Couldn't decode PNG.");
		ret = -1;
		goto out;
	}

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			uint8_t *pix = image + (6 * width * y) + (6 * x);
			uint16_t r = pix[1] << 0 | pix[0] << 8;
			uint16_t g = pix[3] << 0 | pix[2] << 8;
			uint16_t b = pix[5] << 0 | pix[4] << 8;

			uint16_t pixel = 0;
			pixel |= (r & 0xF800);
			pixel |= ((g & 0xFC00) >> 5);
			pixel |= ((b & 0xF800) >> 11);
			fwrite(&pixel, 1, sizeof(pixel), fb);
		}
	}
out:
	fclose(fb);

	return ret;
}

static int draw_error_image_with_text(char *text, char *file_path)
{
	if (!wand) {
		MagickWandGenesis();
		wand = NewMagickWand();
	}

	if (!drawing_wand) {
		drawing_wand = NewDrawingWand();
	}

	if (!pixel_wand) {
		pixel_wand = NewPixelWand();
	}

	PixelSetColor(pixel_wand, "black");

	if (MagickNewImage(wand, 240, 320, pixel_wand) == MagickFalse) {
		fprintf(stderr, "Couldn't use MagickNewImage!");
		return -3;
	}

	MagickSetImageDepth(wand, 16);

	DrawSetFont(drawing_wand, "Courier-New");
	DrawSetFontSize(drawing_wand, 14);
	DrawSetGravity(drawing_wand, NorthWestGravity);
        PixelSetColor(pixel_wand, "green");
        DrawSetFillColor(drawing_wand, pixel_wand);
	DrawAnnotation(drawing_wand, 2, 2, (unsigned char *)text);

	if (MagickDrawImage(wand, drawing_wand) == MagickFalse) {
		fprintf(stderr, "Couldn't use MagickDrawImage!");
		return -2;
	}

	if (MagickWriteImage(wand, file_path) == MagickFalse) {
		fprintf(stderr, "Couldn't use MagickWriteImage!");
		return -1;
	}

	ClearDrawingWand(drawing_wand);

	return 0;
}

static int get_google_maps_image_from_coordinates(double latitude, double longitude, int zoom_level, int width, int height, const char *file_path)
{
	int ret = 0, err = 0;
	FILE *file;
	CURL *curl;
	char URL[1000];

	curl_global_init(CURL_GLOBAL_ALL);

	/* Start the curl session. */
	curl = curl_easy_init();
	if (!curl) {
		return -1;
	}

	/* XXX: Possible buffer overflow? */
	sprintf(URL, "http://maps.googleapis.com/maps/api/staticmap?center=%f,%f&zoom=%d&size=%dx%d&maptype=map&sensor=true&markers=color:blue|size:small|%f,%f", latitude, longitude, zoom_level, width, height, latitude, longitude);

	/* Set the URL. */
	curl_easy_setopt(curl, CURLOPT_URL, URL);

	/* Tell curl to send data to the write_data function. */
//	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);

	file = fopen(file_path, "wb");
	if (!file) {
		fprintf(stderr, "Couldn't open %s\n", file_path);
		ret = -2;
		goto out;
	}

	/* Tell curl to write the data to the file instead of stdout. */
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

	/* Perform the request. */
	err = curl_easy_perform(curl);
	if (err != CURLE_OK) {
		fprintf(stderr, "Couldn't perform the web request: %d", err);
		ret = -1;
		goto out;
	}
out:
	/* Close the file. */
	fclose(file);

	/* Cleanup. */
	curl_easy_cleanup(curl);

	return ret;
}

static void signal_handler(int signal)
{
	fprintf(stderr, "Got signal: %d. \n.", signal);
	die(signal);
}

static void die(int reason)
{
	/* Tell gpsd to stop streaming data. */
	gps_stream(&gpsdata, WATCH_DISABLE, NULL);

	/* Close the connection to gpsd. */
	gps_close(&gpsdata);

	/* Close the log file. */
	fclose(log_file);

	/* Cleanup MagickWand objects. */
	if (wand) {
		DestroyMagickWand(wand);
	}

	if (drawing_wand) {
		DestroyDrawingWand(drawing_wand);
	}

	if (pixel_wand) {
		DestroyPixelWand(pixel_wand);
	}

	fprintf(stderr, "Exiting: %d\n", reason);

	exit(reason);
}

int main(int argc, char *argv[])
{
	/* Set up signal handlers. */
	signal(SIGINT, signal_handler);
	signal(SIGHUP, signal_handler);

	/* Open the stream to gpsd. */
	if (gps_open(NULL, NULL, &gpsdata) != 0) {
		fprintf(stderr,
			"err: no gpsd running or network error: %d, %s\n",
			 errno, gps_errstr(errno));
		exit(EXIT_FAILURE);
	}

	gps_stream(&gpsdata, WATCH_ENABLE, NULL);

	/* Store the time that we started asking for data to keep track of how long we've been running for. */
	time_t start_time = time(NULL);

	/* Create an appropriate file name using from the current date & time. */
	struct tm local_time = *localtime(&start_time);

	char path[500];
	sprintf(path, "/tmp/gps.log-%d-%d-%d-%d:%d:%d.txt", local_time.tm_year + 1900,
						local_time.tm_mon + 1,
						local_time.tm_mday,
						local_time.tm_hour,
						local_time.tm_min,
						local_time.tm_sec);
	/* Open the log file. */
	log_file = fopen(path, "w");
	if (!log_file) {
		fprintf(stderr, "Couldn't open file at path: %s\n", path);
		die(-1);
	}

	/* Write initial information in the log. */
	fprintf(log_file, "// Log format: longitude, latitude, altitude, speed, climb\n");

	while (1) {
		if (!gps_waiting(&gpsdata, 5000000)) {
			fprintf(stderr, "Error: Timed out.\n");
			die(-1);
		} else {
			if (gps_read(&gpsdata) == -1) {
				fprintf(stderr, "Error: couldn't read.\n");
				die(-2);
			} else {
				int fix_mode = gpsdata.fix.mode;
				if (fix_mode == MODE_NO_FIX || fix_mode == MODE_NOT_SEEN) {
					char text[100];
					sprintf(text, "No fix ... %.0f seconds.\n", difftime(time(NULL), start_time));
					if (draw_error_image_with_text(text, "/tmp/gps.png") < 0) {
						fprintf(stderr, "Couldn't draw image.\n");
						die(-1);
					}

                                        if (blit_png_at_path_to_framebuffer("/tmp/gps.png", "/dev/fb1") < 0) {
                                                fprintf(stderr, "[No Fix] Couldn't write image to framebuffer.\n");
                                                die(-2);
                                        }

				} else {
					double latitude = gpsdata.fix.latitude;
					double longitude = gpsdata.fix.longitude;

					fprintf(stdout, "Fix mode: %d\n\t Lat: %f %c\n\t Long: %f %c\n", fix_mode, latitude,
											  (latitude < 0) ? 'S' : 'N', longitude, (longitude < 0) ? 'W' : 'E');

					/* Get the Google Static map. */
					int ret = get_google_maps_image_from_coordinates(latitude, longitude, 17, 240, 320, "/tmp/gps.png");
					if (ret < 0) {
						fprintf(stderr, "Couldn't get image from google maps. Err: %d", ret);
						// TODO: Display an error image on the LCD.
					}

                                        if (blit_png_at_path_to_framebuffer("/tmp/gps.png", "/dev/fb1") < 0) {
                                                fprintf(stderr, "Couldn't write image to framebuffer.\n");
                                                die(-2);
                                  	}

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
