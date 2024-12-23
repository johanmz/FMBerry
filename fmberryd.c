/*
	FMBerry - an cheap and easy way of transmitting music with your Pi.
    Copyright (C) 2011-2013 by Manawyrm
	Copyright (C) 2013      by Andrey Chilikin (https://github.com/achilikin)
	Switch from sysfs gpio interface to libgpio v2.1.3 by johanmz (https://github.com/johanmz), 2024

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "fmberryd.h"
#include "rpi_pin.h"
#include "ns741.h"

#include <poll.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <confuse.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <gpiod.h>

#include <errno.h>

#define RPI_REVISION RPI_REV2

// RDS interrupt pin
int rdsint = 17;

// LED pin number
int ledpin = -1;

mmr70_data_t mmr70;

static cfg_t *cfg;
static volatile int run = 1;
static int start_daemon = 1;

static struct gpiod_request_config *req_cfg = NULL;
static struct gpiod_line_request *request = NULL;
static struct gpiod_line_settings *settings;
static struct gpiod_line_config *line_cfg;
static struct gpiod_chip *chip;
static int rds;

// ideally the rds_input and led_output gpiod functions should have been placed in rpi_pin.c, however I could not make it work due to the static struct.
// maybe there's someone out there with more C knowledge than me to make it work
static struct gpiod_line_request *request_rds_input_line(const char *chip_path,
						     unsigned int offset,
						     const char *consumer)
{

	int ret;

	chip = gpiod_chip_open(chip_path);
	if (!chip)
		return NULL;

	settings = gpiod_line_settings_new();
	if (!settings)
		goto close_chip;

	gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
	gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_FALLING);

	line_cfg = gpiod_line_config_new();
	if (!line_cfg)
		goto free_settings;

	ret = gpiod_line_config_add_line_settings(line_cfg, &offset, 1,
						  settings);
	if (ret)
		goto free_line_config;

	if (consumer) {
		req_cfg = gpiod_request_config_new();
		if (!req_cfg)
			goto free_line_config;

		gpiod_request_config_set_consumer(req_cfg, consumer);
	}

	request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
	return request;


free_line_config:
	gpiod_line_config_free(line_cfg);

free_settings:
	gpiod_line_settings_free(settings);

close_chip:
	gpiod_chip_close(chip);

	return NULL;
}

static struct gpiod_request_config *led_req_cfg = NULL;
static struct gpiod_line_request *led_request = NULL;
static struct gpiod_line_settings *led_settings;
static struct gpiod_line_config *led_line_cfg;

static struct gpiod_line_request *
request_led_output_line(unsigned int offset,
		    enum gpiod_line_value led_value, const char *consumer)
{
	int ret;

	if (!chip)
		return NULL;

	led_settings = gpiod_line_settings_new();
	if (!led_settings)
		goto led_close;

	gpiod_line_settings_set_direction(led_settings, GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_settings_set_output_value(led_settings, led_value);

	led_line_cfg = gpiod_line_config_new();
	if (!led_line_cfg)
		goto led_free_settings;

	ret = gpiod_line_config_add_line_settings(led_line_cfg, &offset, 1,
						  led_settings);
	if (ret)
		goto led_free_line_config;

	if (consumer) {
		led_req_cfg = gpiod_request_config_new();
		if (!led_req_cfg)
			goto led_free_line_config;

		gpiod_request_config_set_consumer(led_req_cfg, consumer);
	}

	request = gpiod_chip_request_lines(chip, led_req_cfg, led_line_cfg);
	return request;


led_free_line_config:
	gpiod_line_config_free(led_line_cfg);

led_free_settings:
	gpiod_line_settings_free(led_settings);

led_close:
	return NULL;
}

static enum gpiod_line_value toggle_line_value(enum gpiod_line_value value)
{
	return (value == GPIOD_LINE_VALUE_ACTIVE) ? GPIOD_LINE_VALUE_INACTIVE :
						    GPIOD_LINE_VALUE_ACTIVE;
}


int main(int argc, char **argv)
{
	
	
	//Check if user == root
	/*
	if(geteuid() != 0)
	{
	  puts("Please run this software as root!");
	  exit(EXIT_FAILURE);
	}
    */

	// check for non-daemon mode for debugging
	for(int i = 1; i < argc; i++) {
		if (str_is(argv[i], "nodaemon")) {
			start_daemon = 0;
			break;
		}
	}

	if (start_daemon) {
		//Init daemon, can be replaced with daemon() call
		pid_t pid;

		pid = fork();
		if (pid < 0)
		{
			exit(EXIT_FAILURE);
		}
		if (pid > 0)
		{
			exit(EXIT_SUCCESS);
		}

		umask(0);

		//We are now running as the forked child process.
		openlog(argv[0],LOG_NOWAIT|LOG_PID,LOG_USER);

		pid_t sid;
		sid = setsid();
		if (sid < 0)
		{
			syslog(LOG_ERR, "Could not create process group\n");
			exit(EXIT_FAILURE);
		}

		if ((chdir("/")) < 0)
		{
			syslog(LOG_ERR, "Could not change working directory to /\n");
			exit(EXIT_FAILURE);
		}
	}
	else {
		// open syslog for non-daemon mode
		openlog(argv[0],LOG_NOWAIT|LOG_PID,LOG_USER);
	}

	//Read configuration file
	cfg_opt_t opts[] =
	{
		CFG_INT("i2cbus", 1, CFGF_NONE),
		CFG_INT("frequency", 99800, CFGF_NONE),
		CFG_BOOL("stereo", 1, CFGF_NONE),
		CFG_BOOL("rdsenable", 1, CFGF_NONE),
		CFG_BOOL("poweron", 1, CFGF_NONE),
		CFG_BOOL("tcpbindlocal", 1, CFGF_NONE),
		CFG_INT("tcpport", 42516, CFGF_NONE),
		CFG_INT("txpower", 3, CFGF_NONE),
		CFG_BOOL("gain", 0, CFGF_NONE),
		CFG_INT("volume", 3, CFGF_NONE),
		CFG_INT("rdspin", 17, CFGF_NONE),
		CFG_STR("rdsid", "", CFGF_NONE),
		CFG_STR("rdstext", "", CFGF_NONE),
		CFG_INT("ledpin", 27, CFGF_NONE),
		CFG_END()
	};

	cfg = cfg_init(opts, CFGF_NONE);
	if (cfg_parse(cfg, "/etc/fmberry.conf") == CFG_PARSE_ERROR)
		return 1;

	// get LED and RDS pin number
	ledpin = cfg_getint(cfg, "ledpin");
	rdsint = cfg_getint(cfg, "rdspin");

	// Init I2C bus and transmitter with initial frequency and state
	if (ns741_init(cfg_getint(cfg, "i2cbus"), cfg_getint(cfg, "frequency")) == -1)
	{
		syslog(LOG_ERR, "Init failed! Double-check hardware and try again!\n");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Successfully initialized ns741 transmitter.\n");

	int nfds;
	struct pollfd  polls[1];

	// open TCP listener socket, will exit() in case of error
	int lst = ListenTCP(cfg_getint(cfg, "tcpport"));
	polls[0].fd = lst;
	polls[0].events = POLLIN;
	nfds = 1;

	// initialize data structure for 'status' command
	bzero(&mmr70, sizeof(mmr70));
	mmr70.frequency = cfg_getint(cfg, "frequency");
	mmr70.power     = cfg_getbool(cfg, "poweron");
	mmr70.txpower   = cfg_getint(cfg, "txpower");
	mmr70.mute      = 0;
	mmr70.gain      = cfg_getbool(cfg, "gain");
	mmr70.volume    = cfg_getint(cfg, "volume");
	mmr70.stereo    = cfg_getbool(cfg, "stereo");
	mmr70.rds       = cfg_getbool(cfg, "rdsenable");
	strncpy(mmr70.rdsid, cfg_getstr(cfg, "rdsid"), 8);
	strncpy(mmr70.rdstext, cfg_getstr(cfg, "rdstext"), 64);

	// apply configuration parameters
	ns741_txpwr(mmr70.txpower);
	ns741_mute(mmr70.mute);
	ns741_stereo(mmr70.stereo);
	ns741_rds_set_progname(mmr70.rdsid);
	ns741_rds_set_radiotext(mmr70.rdstext);
	ns741_power(mmr70.power);
	ns741_input_gain(mmr70.gain);
    ns741_volume(mmr70.volume);
	// Use RPI_REV1 for earlier versions of Raspberry Pi
	rpi_pin_init(RPI_REVISION);


	// initialize RDS pin
	static const char *const chip_path = "/dev/gpiochip0";
	unsigned int line_offset = rdsint;

	struct gpiod_edge_event_buffer *event_buffer;
	struct gpiod_line_request *request;
	struct gpiod_edge_event *event;
	int i, ret, event_buf_size;

	int rds=-1;
	enum gpiod_line_value led_value = GPIOD_LINE_VALUE_ACTIVE;

	if (mmr70.rds)
	{
		request = request_rds_input_line(chip_path, line_offset,"fmberryd");
		if (!request) {
			syslog(LOG_ERR, "Failed to request RDS line: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		event_buf_size = 1;
		event_buffer = gpiod_edge_event_buffer_new(event_buf_size);
		if (!event_buffer) {
			syslog(LOG_ERR, "Failed to create GPIO event buffer");
			exit(EXIT_FAILURE);
		}	
		rds = 1;
		nfds = 2;
		if (ledpin > 0) {
			led_request = request_led_output_line(ledpin, GPIOD_LINE_VALUE_ACTIVE, "fmberryd");
			if (!led_request){
				syslog(LOG_ERR, "Failed to request LED line: %s\n", strerror(errno));
				exit(EXIT_FAILURE);
			}

		}

		ns741_rds(1);
		ns741_rds_isr(); // send first two bytes
	}

	// main polling loop
	int ledcounter = 0;
	int rds_wait_timeout = 0;

	while(run) {
		// next request for RDS data should come in 21ms so we wait max 25ms
		ret = gpiod_line_request_wait_edge_events (request, 25000000);
		if (ret < 0) {
			syslog(LOG_ERR, "RDS wait event notification failed\n");
			ret = -1;
			break;
		} else if (ret == 0) {
			if (++rds_wait_timeout<=25)
				syslog(LOG_ERR, "RDS wait event notification timeout (max 25 messages) %d\n", rds_wait_timeout); //avoid filling up the journal in no-time if there's an issue
			continue;
		}
		ret = gpiod_line_request_read_edge_events(request, event_buffer, event_buf_size);
		if (ret < 0) {
			syslog(LOG_ERR, "RDS read last event notification failed\n");
			ret = -1;
			break;
		}
		ns741_rds_isr();
		if (ledpin > 0) {
				ledcounter++;
				if (!(ledcounter % 80)) {
					led_value = toggle_line_value(led_value);
					gpiod_line_request_set_value(led_request, ledpin, led_value);
				}
		}
	
		if (poll(polls, nfds, -1) < 0)
		break;

		if (polls[0].revents)
			ProcessTCP(lst, &mmr70);
	}

	// clean up at exit
	ns741_power(0);

	if (rds){
		if (ledpin > 0){
			gpiod_request_config_free(led_req_cfg);
			gpiod_line_config_free(led_line_cfg);
			gpiod_line_settings_free(led_settings);
		}
		gpiod_request_config_free(req_cfg);
		gpiod_line_config_free(line_cfg);
		gpiod_line_settings_free(settings);
		gpiod_chip_close(chip);	
	}

	close(lst);
	closelog();
	
	return ret;
}

int ListenTCP(uint16_t port)
{
	/* Socket erstellen - TCP, IPv4, keine Optionen */
	int lsd = socket(AF_INET, SOCK_STREAM, 0);

	/* IPv4, Port: 1111, jede IP-Adresse akzeptieren */
	struct sockaddr_in saddr;
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	if (cfg_getbool(cfg, "tcpbindlocal"))
	{
		syslog(LOG_NOTICE, "Binding to localhost.\n");
    	saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
	else
	{
		syslog(LOG_NOTICE, "Binding to any interface.\n");
    	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

	//Important! Makes sure you can restart the daemon without any problems!
	const int       optVal = 1;
	const socklen_t optLen = sizeof(optVal);

	int rtn = setsockopt(lsd, SOL_SOCKET, SO_REUSEADDR, (void*) &optVal, optLen);

	//assert(rtn == 0);   /* this is optional */

	/* Socket an Port binden */
	if (bind(lsd, (struct sockaddr*) &saddr, sizeof(saddr)) < 0) {
  		//whoops. Could not listen
  		syslog(LOG_ERR, "Could not bind to TCP port! Terminated.\n");
  		exit(EXIT_FAILURE);
	 }

  	syslog(LOG_NOTICE, "Successfully started daemon\n");
	/* Auf Socket horchen (Listen) */
	listen(lsd, 10);

	return lsd;
}

// for 'status' command
static float txpower[4] = { 0.5, 0.8, 1.0, 2.0 };

int ProcessTCP(int sock, mmr70_data_t *pdata)
{
	/* Puffer und Strukturen anlegen */
	struct sockaddr_in clientaddr;
	socklen_t clen = sizeof(clientaddr);
	char buffer[512];
	bzero(buffer, sizeof(buffer));

	/* Auf Verbindung warten, bei Verbindung Connected-Socket erstellen */
	int csd = accept(sock, (struct sockaddr *)&clientaddr, &clen);

	struct pollfd  pol;
	pol.fd = csd;
	pol.events = POLLRDNORM;

	// just to be on a safe side check if data is available
	if (poll(&pol, 1, 1000) <= 0) {
		close(csd);
		return -1;
	}

	int len  = recv(csd, buffer, sizeof(buffer) - 2, 0);
	buffer[len] = '\0';
	char *end = buffer + len - 1;
	// remove any trailing spaces
	while((end != buffer) && (*end <= ' ')) {
		*end-- = '\0';
	}

	do {
		const char *arg;

		if (str_is_arg(buffer, "set freq", &arg))
		{
			int frequency = atoi(arg);

			if ((frequency >= 76000) && (frequency <= 108000))
			{
				syslog(LOG_NOTICE, "Changing frequency...\n");
				ns741_set_frequency(frequency);
				pdata->frequency = frequency;
			}
			else
			{
				syslog(LOG_NOTICE, "Bad frequency.\n");
			}
			break;
		}

		if (str_is(buffer, "poweroff"))
		{
			ns741_power(0);
			pdata->power = 0;
			break;
		}

		if (str_is(buffer, "poweron"))
		{
			ns741_power(1);
			ns741_rds(1);
			ns741_rds_reset_radiotext();
			pdata->power = 1;
			break;
		}

		if (str_is(buffer, "muteon"))
		{
			ns741_mute(1);
			pdata->mute = 1;
			break;
		}

		if (str_is(buffer, "muteoff"))
		{
			ns741_mute(0);
			pdata->mute = 0;
			break;
		}

		if (str_is(buffer, "gainlow"))
		{
			ns741_input_gain(1);
			pdata->gain = 1;
			break;
		}

		if (str_is(buffer, "gainoff"))
		{
			ns741_input_gain(0);
			pdata->gain = 0;
			break;
		}

		if (str_is_arg(buffer, "set volume", &arg))
		{
			int volume = atoi(arg);

			if ((volume >= 0) && (volume <= 6))
			{
				syslog(LOG_NOTICE, "Changing volume level...\n");
				ns741_volume(volume);
				pdata->volume = volume;
			}
			else
			{
				syslog(LOG_NOTICE, "Bad volume level. Range 0-6\n");
			}
			break;
		}

		if (str_is_arg(buffer, "set stereo", &arg))
		{
			if (str_is(arg, "on"))
			{
				syslog(LOG_NOTICE, "Enabling stereo signal...\n");
				ns741_stereo(1);
				pdata->stereo = 1;
				break;
			}
			if (str_is(arg, "off"))
			{
				syslog(LOG_NOTICE, "Disabling stereo signal...\n");
				ns741_stereo(0);
				pdata->stereo = 0;
			}
			break;
		}

		if (str_is_arg(buffer, "set txpwr", &arg))
		{
			int txpwr = atoi(arg);

			if ((txpwr >= 0) && (txpwr <= 3))
			{
				syslog(LOG_NOTICE, "Changing transmit power...\n");
				ns741_txpwr(txpwr);
				pdata->txpower = txpwr;
			}
			else
			{
				syslog(LOG_NOTICE, "Bad transmit power. Range 0-3\n");
			}
			break;
		}

		if (str_is_arg(buffer, "set rdstext", &arg))
		{
			strncpy(pdata->rdstext, arg, 64);
			ns741_rds_set_radiotext(pdata->rdstext);
			break;
		}

		if (str_is_arg(buffer, "set rdsid", &arg))
		{
			bzero(pdata->rdsid, sizeof(pdata->rdsid));
			strncpy(pdata->rdsid, arg, 8);
			// ns741_rds_set_progname() will pad rdsid with spaces if needed
			ns741_rds_set_progname(pdata->rdsid);
			ns741_rds_reset_radiotext();
			break;
		}

		if (str_is(buffer, "die") || str_is(buffer, "stop"))
		{
			run = 0;
			syslog(LOG_NOTICE, "Shutting down.\n");
			break;
		}

		if (str_is(buffer, "status"))
		{
			bzero(buffer, sizeof(buffer));
			sprintf(buffer, "freq: %dKHz txpwr: %.2fmW power: '%s' mute: '%s' gain: '%s' volume: '%d' stereo: '%s' rds: '%s' rdsid: '%s' rdstext: '%s'\n",
				pdata->frequency,
				txpower[pdata->txpower],
				pdata->power ? "on" : "off",
				pdata->mute ? "on" : "off",
				pdata->gain ? "on" : "off",
				pdata->volume,
				pdata->stereo ? "on" : "off",
				pdata->rds ? "on" : "off",
				pdata->rdsid, pdata->rdstext);
			write(csd, buffer, strlen(buffer) + 1);
			break;
		}

	} while(0);

	close(csd);
	return 0;
}

// helper string compare functions
int str_is(const char *str, const char *is)
{
	if (strcmp(str, is) == 0)
		return 1;
	return 0;
}

int str_is_arg(const char *str, const char *is, const char **arg)
{
	size_t len = strlen(is);
	if (strncmp(str, is, len) == 0) {
		str = str + len;
		// remove any leading spaces from the arg
		while(*str && (*str <= ' '))
			str++;
		*arg = str;
		return 1;
	}
	return 0;
}
