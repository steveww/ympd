/* ympd
   (c) 2013-2014 Andrew Karpow <andy@ndyk.de>
   This project's homepage is: http://www.ympd.org
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <mpd/client.h>

#include "mpd_client.h"
#include "config.h"
#include "json_encode.h"

extern const char *template;

/* forward declaration */
static int mpd_notify_callback(struct mg_connection *c, enum mg_event ev);

const char * mpd_cmd_strs[] = {
    MPD_CMDS(GEN_STR)
};

char * get_arg1 (char *p) {
	return strchr(p, ',') + 1;
}

char * get_arg2 (char *p) {
	return get_arg1(get_arg1(p));
}

char *parse_templ(const char *templ, struct t_meta data) {
    static char buffer[1024];
    char *bptr = buffer;
    const char *dptr;

	int is_path = 0;
    while(*templ != 0) {
        dptr = 0;
        if(*templ == '%') {
            switch(*(templ + 1)) {
                case 'A':
                    dptr = data.artist;
                    break;
                case 'a':
                    dptr = data.album;
                    break;
                case 't':
                    dptr = data.track;
                    break;
                case 'T':
                    dptr = data.title;
                    break;
				case 'P':
					dptr = data.uri;
					is_path = 1;
					break;
                case '%':
                    *bptr++ = *templ;
                    templ += 2;
                    break;
                default:
                    printf("Unrecognised token %c\n", *(templ + 1));
            }
            if(dptr != 0) {
				if(is_path) {
					char *pptr = strdup(dptr);
					char *dir = dirname(pptr);
					while(*dir != 0) {
						*bptr++ = *dir++;
					}
					free(pptr);
				} else {
					while(*dptr != 0) {
						if(*dptr == '/' || *dptr == '?') {
							*bptr = '_';
						} else {
							*bptr = *dptr;
						}
						bptr++;
						dptr++;
					}
				}
			}
			templ += 2;
        } else {
            *bptr++ = *templ++;
        }
    }
	*bptr = 0;

    return buffer;
}

static inline enum mpd_cmd_ids get_cmd_id(char *cmd)
{
    for(int i = 0; i < sizeof(mpd_cmd_strs)/sizeof(mpd_cmd_strs[0]); i++)
        if(!strncmp(cmd, mpd_cmd_strs[i], strlen(mpd_cmd_strs[i])))
            return i;

    return -1;
}

int callback_mpd(struct mg_connection *c)
{
    enum mpd_cmd_ids cmd_id = get_cmd_id(c->content);
    size_t n = 0;
    unsigned int uint_buf, uint_buf_2;
    int int_buf;
    char *p_charbuf = NULL, *token;

    if(cmd_id == -1)
        return MG_TRUE;

    if(mpd.conn_state != MPD_CONNECTED && cmd_id != MPD_API_SET_MPDHOST &&
        cmd_id != MPD_API_GET_MPDHOST && cmd_id != MPD_API_SET_MPDPASS)
        return MG_TRUE;

    switch(cmd_id)
    {
        case MPD_API_UPDATE_DB:
            mpd_run_update(mpd.conn, NULL);
            break;
        case MPD_API_SET_PAUSE:
            mpd_run_toggle_pause(mpd.conn);
            break;
        case MPD_API_SET_PREV:
            mpd_run_previous(mpd.conn);
            break;
        case MPD_API_SET_NEXT:
            mpd_run_next(mpd.conn);
            break;
        case MPD_API_SET_PLAY:
            mpd_run_play(mpd.conn);
            break;
        case MPD_API_SET_STOP:
            mpd_run_stop(mpd.conn);
            break;
        case MPD_API_RM_ALL:
            mpd_run_clear(mpd.conn);
            break;
        case MPD_API_RM_TRACK:
            if(sscanf(c->content, "MPD_API_RM_TRACK,%u", &uint_buf))
                mpd_run_delete_id(mpd.conn, uint_buf);
            break;
        case MPD_API_PLAY_TRACK:
            if(sscanf(c->content, "MPD_API_PLAY_TRACK,%u", &uint_buf))
                mpd_run_play_id(mpd.conn, uint_buf);
            break;
        case MPD_API_TOGGLE_RANDOM:
            if(sscanf(c->content, "MPD_API_TOGGLE_RANDOM,%u", &uint_buf))
                mpd_run_random(mpd.conn, uint_buf);
            break;
        case MPD_API_TOGGLE_REPEAT:
            if(sscanf(c->content, "MPD_API_TOGGLE_REPEAT,%u", &uint_buf))
                mpd_run_repeat(mpd.conn, uint_buf);
            break;
        case MPD_API_TOGGLE_CONSUME:
            if(sscanf(c->content, "MPD_API_TOGGLE_CONSUME,%u", &uint_buf))
                mpd_run_consume(mpd.conn, uint_buf);
            break;
        case MPD_API_TOGGLE_SINGLE:
            if(sscanf(c->content, "MPD_API_TOGGLE_SINGLE,%u", &uint_buf))
                mpd_run_single(mpd.conn, uint_buf);
            break;
        case MPD_API_TOGGLE_CROSSFADE:
            if(sscanf(c->content, "MPD_API_TOGGLE_CROSSFADE,%u", &uint_buf))
                mpd_run_crossfade(mpd.conn, uint_buf);
            break;
        case MPD_API_GET_OUTPUTS:
            mpd.buf_size = mpd_put_outputs(mpd.buf, 1);
            c->callback_param = NULL;
            mpd_notify_callback(c, MG_POLL);
            break;
        case MPD_API_TOGGLE_OUTPUT:
            if (sscanf(c->content, "MPD_API_TOGGLE_OUTPUT,%u,%u", &uint_buf, &uint_buf_2)) {
                if (uint_buf_2)
                    mpd_run_enable_output(mpd.conn, uint_buf);
                else
                    mpd_run_disable_output(mpd.conn, uint_buf);
            }
            break;
        case MPD_API_SET_VOLUME:
            if(sscanf(c->content, "MPD_API_SET_VOLUME,%ud", &uint_buf) && uint_buf <= 100)
                mpd_run_set_volume(mpd.conn, uint_buf);
            break;
        case MPD_API_SET_SEEK:
            if(sscanf(c->content, "MPD_API_SET_SEEK,%u,%u", &uint_buf, &uint_buf_2))
                mpd_run_seek_id(mpd.conn, uint_buf, uint_buf_2);
            break;
        case MPD_API_GET_QUEUE:
            if(sscanf(c->content, "MPD_API_GET_QUEUE,%u", &uint_buf))
                n = mpd_put_queue(mpd.buf, uint_buf);
            break;
        case MPD_API_GET_BROWSE:
            p_charbuf = strdup(c->content);
            if(strcmp(strtok(p_charbuf, ","), "MPD_API_GET_BROWSE"))
                goto out_browse;

            uint_buf = strtoul(strtok(NULL, ","), NULL, 10);
            if((token = strtok(NULL, ",")) == NULL)
                goto out_browse;

			free(p_charbuf);
            p_charbuf = strdup(c->content);
            n = mpd_put_browse(mpd.buf, get_arg2(p_charbuf), uint_buf);
out_browse:
			free(p_charbuf);
            break;
        case MPD_API_ADD_TRACK:
            p_charbuf = strdup(c->content);
            if(strcmp(strtok(p_charbuf, ","), "MPD_API_ADD_TRACK"))
                goto out_add_track;

            if((token = strtok(NULL, ",")) == NULL)
                goto out_add_track;

			free(p_charbuf);
            p_charbuf = strdup(c->content);
            mpd_run_add(mpd.conn, get_arg1(p_charbuf));
out_add_track:
            free(p_charbuf);
            break;
        case MPD_API_INSERT_TRACK:
            p_charbuf = strdup(c->content);
            if(strcmp(strtok(p_charbuf, ","), "MPD_API_INSERT_TRACK"))
                goto out_insert_track;

            if((token = strtok(NULL, ",")) == NULL)
                goto out_insert_track;

			free(p_charbuf);
            p_charbuf = strdup(c->content);
			struct mpd_status *status = mpd_run_status(mpd.conn);
			int playing_pos = mpd_status_get_song_pos(status) + 1;
			mpd_status_free(status);
			mpd_run_add_id_to(mpd.conn, get_arg1(p_charbuf), playing_pos);
out_insert_track:
            free(p_charbuf);
            break;
		case MPD_API_MOVE_TRACK:
            if(sscanf(c->content, "MPD_API_MOVE_TRACK,%u,%u", &uint_buf, &uint_buf_2)) {
                mpd_run_move(mpd.conn, uint_buf, uint_buf_2);
			}
			break;
        case MPD_API_ADD_PLAYLIST:
            p_charbuf = strdup(c->content);
            if(strcmp(strtok(p_charbuf, ","), "MPD_API_ADD_PLAYLIST"))
                goto out_playlist;

            if((token = strtok(NULL, ",")) == NULL)
                goto out_playlist;

			free(p_charbuf);
            p_charbuf = strdup(c->content);
            mpd_run_load(mpd.conn, get_arg1(p_charbuf));
out_playlist:
            free(p_charbuf);
            break;
		case MPD_API_RM_PLAYLIST:
			p_charbuf = strdup(c->content);
			if(strcmp(strtok(p_charbuf, ","), "MPD_API_RM_PLAYLIST"))
				goto out_rm_playlist;

			if((token = strtok(NULL, ",")) == NULL)
				goto out_rm_playlist;

			free(p_charbuf);
			p_charbuf = strdup(c->content);
			mpd_run_rm(mpd.conn, get_arg1(p_charbuf));
out_rm_playlist:
			free(p_charbuf);
			break;
        case MPD_API_SAVE_QUEUE:
            p_charbuf = strdup(c->content);
            if(strcmp(strtok(p_charbuf, ","), "MPD_API_SAVE_QUEUE"))
                goto out_save_queue;

            if((token = strtok(NULL, ",")) == NULL)
                goto out_save_queue;

			free(p_charbuf);
            p_charbuf = strdup(c->content);
            mpd_run_save(mpd.conn, get_arg1(p_charbuf));
out_save_queue:
            free(p_charbuf);
            break;
        case MPD_API_SEARCH:
            p_charbuf = strdup(c->content);
            if(strcmp(strtok(p_charbuf, ","), "MPD_API_SEARCH"))
				goto out_search;

            if((token = strtok(NULL, ",")) == NULL)
                goto out_search;

			free(p_charbuf);
            p_charbuf = strdup(c->content);
            n = mpd_search(mpd.buf, get_arg1(p_charbuf));
out_search:
            free(p_charbuf);
            break;
#ifdef WITH_MPD_HOST_CHANGE
        /* Commands allowed when disconnected from MPD server */
        case MPD_API_SET_MPDHOST:
            int_buf = 0;
            p_charbuf = strdup(c->content);
            if(strcmp(strtok(p_charbuf, ","), "MPD_API_SET_MPDHOST"))
                goto out_host_change;

            if((int_buf = strtol(strtok(NULL, ","), NULL, 10)) <= 0)
                goto out_host_change;

            if((token = strtok(NULL, ",")) == NULL)
                goto out_host_change;

            strncpy(mpd.host, token, sizeof(mpd.host));
            mpd.port = int_buf;
            mpd.conn_state = MPD_RECONNECT;
            free(p_charbuf);
            return MG_TRUE;
out_host_change:
            free(p_charbuf);
            break;
        case MPD_API_GET_MPDHOST:
            n = snprintf(mpd.buf, MAX_SIZE, "{\"type\":\"mpdhost\", \"data\": "
                "{\"host\" : \"%s\", \"port\": \"%d\", \"passwort_set\": %s}"
                "}", mpd.host, mpd.port, mpd.password ? "true" : "false");
            break;
        case MPD_API_SET_MPDPASS:
            p_charbuf = strdup(c->content);
            if(strcmp(strtok(p_charbuf, ","), "MPD_API_SET_MPDPASS"))
                goto out_set_pass;

            if((token = strtok(NULL, ",")) == NULL)
                goto out_set_pass;

            if(mpd.password)
                free(mpd.password);

            mpd.password = strdup(token);
            mpd.conn_state = MPD_RECONNECT;
            free(p_charbuf);
            return MG_TRUE;
out_set_pass:
            free(p_charbuf);
            break;
#endif
		case MPD_API_GET_STATS:
			{
				struct mpd_stats *stats = mpd_run_stats(mpd.conn);
				unsigned int artists = mpd_stats_get_number_of_artists(stats);
				unsigned int albums = mpd_stats_get_number_of_albums(stats);
				unsigned int songs = mpd_stats_get_number_of_songs(stats);
				unsigned long db_update = mpd_stats_get_db_update_time(stats);
				unsigned long total_time = mpd_stats_get_db_play_time(stats);
				n = snprintf(mpd.buf, MAX_SIZE, "{\"type\":\"mpdstats\", \"data\":"
					"{\"artists\": %u, \"albums\": %u, \"songs\": %u, "
					"\"update\": %lu, \"ttime\": %lu}}", artists, albums, songs, db_update, total_time);
				mpd_stats_free(stats);
			}
			break;
		default:
			printf("Unknown token %d\n", cmd_id);
			break;
    }

    if(mpd.conn_state == MPD_CONNECTED && mpd_connection_get_error(mpd.conn) != MPD_ERROR_SUCCESS)
    {
        n = snprintf(mpd.buf, MAX_SIZE, "{\"type\":\"error\", \"data\": \"%s\"}", 
            mpd_connection_get_error_message(mpd.conn));

        /* Try to recover error */
        if (!mpd_connection_clear_error(mpd.conn))
            mpd.conn_state = MPD_FAILURE;
    }

    if(n > 0)
        mg_websocket_write(c, 1, mpd.buf, n);

    return MG_TRUE;
}

int mpd_close_handler(struct mg_connection *c)
{
    /* Cleanup session data */
    if(c->connection_param)
        free(c->connection_param);
    return 0;
}

static int mpd_notify_callback(struct mg_connection *c, enum mg_event ev) {
    size_t n;

    if(!c->is_websocket)
        return MG_TRUE;

    if(c->callback_param)
    {
        /* error message? */
        n = snprintf(mpd.buf, MAX_SIZE, "{\"type\":\"error\",\"data\":\"%s\"}", 
            (const char *)c->callback_param);

        mg_websocket_write(c, 1, mpd.buf, n);
        return MG_TRUE;
    }

    if(!c->connection_param)
        c->connection_param = calloc(1, sizeof(struct t_mpd_client_session));

    struct t_mpd_client_session *s = (struct t_mpd_client_session *)c->connection_param;

    if(mpd.conn_state != MPD_CONNECTED) {
        n = snprintf(mpd.buf, MAX_SIZE, "{\"type\":\"disconnected\"}");
        mg_websocket_write(c, 1, mpd.buf, n);
    }
    else
    {
        mg_websocket_write(c, 1, mpd.buf, mpd.buf_size);

        if(s->song_id != mpd.song_id)
        {
            n = mpd_put_current_song(mpd.buf);
            mg_websocket_write(c, 1, mpd.buf, n);
            s->song_id = mpd.song_id;
        }

        if(s->queue_version != mpd.queue_version)
        {
            n = snprintf(mpd.buf, MAX_SIZE, "{\"type\":\"update_queue\"}");
            mg_websocket_write(c, 1, mpd.buf, n);
            s->queue_version = mpd.queue_version;
        }
    }

    return MG_TRUE;
}

void mpd_poll(struct mg_server *s)
{
    switch (mpd.conn_state) {
        case MPD_DISCONNECTED:
            /* Try to connect */
            fprintf(stdout, "MPD Connecting to %s:%d\n", mpd.host, mpd.port);
            mpd.conn = mpd_connection_new(mpd.host, mpd.port, 3000);
            if (mpd.conn == NULL) {
                fprintf(stderr, "Out of memory.");
                mpd.conn_state = MPD_FAILURE;
                return;
            }

            if (mpd_connection_get_error(mpd.conn) != MPD_ERROR_SUCCESS) {
                fprintf(stderr, "MPD connection: %s\n", mpd_connection_get_error_message(mpd.conn));
                for (struct mg_connection *c = mg_next(s, NULL); c != NULL; c = mg_next(s, c))
                {
                    c->callback_param = (void *)mpd_connection_get_error_message(mpd.conn);
                    mpd_notify_callback(c, MG_POLL);
                }
                mpd.conn_state = MPD_FAILURE;
                return;
            }

            if(mpd.password && !mpd_run_password(mpd.conn, mpd.password))
            {
                fprintf(stderr, "MPD connection: %s\n", mpd_connection_get_error_message(mpd.conn));
                for (struct mg_connection *c = mg_next(s, NULL); c != NULL; c = mg_next(s, c))
                {
                    c->callback_param = (void *)mpd_connection_get_error_message(mpd.conn);
                    mpd_notify_callback(c, MG_POLL);
                }
                mpd.conn_state = MPD_FAILURE;
                return;
            }

            fprintf(stderr, "MPD connected.\n");
            mpd_connection_set_timeout(mpd.conn, 10000);
            mpd.conn_state = MPD_CONNECTED;
            /* write outputs */
            mpd.buf_size = mpd_put_outputs(mpd.buf, 1);
            for (struct mg_connection *c = mg_next(s, NULL); c != NULL; c = mg_next(s, c))
            {
                c->callback_param = NULL;
                mpd_notify_callback(c, MG_POLL);
            }
            break;

        case MPD_FAILURE:
            fprintf(stderr, "MPD connection failed.\n");

        case MPD_DISCONNECT:
        case MPD_RECONNECT:
            if(mpd.conn != NULL)
                mpd_connection_free(mpd.conn);
            mpd.conn = NULL;
            mpd.conn_state = MPD_DISCONNECTED;
            break;

        case MPD_CONNECTED:
            mpd.buf_size = mpd_put_state(mpd.buf, &mpd.song_id, &mpd.queue_version);
            for (struct mg_connection *c = mg_next(s, NULL); c != NULL; c = mg_next(s, c))
            {
                c->callback_param = NULL;
                mpd_notify_callback(c, MG_POLL);
            }
            mpd.buf_size = mpd_put_outputs(mpd.buf, 0);
            for (struct mg_connection *c = mg_next(s, NULL); c != NULL; c = mg_next(s, c))
            {
                c->callback_param = NULL;
                mpd_notify_callback(c, MG_POLL);
            }
            break;
    }
}

char* mpd_get_title(struct mpd_song const *song)
{
    char *str;

    str = (char *)mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
    if(str == NULL){
        str = basename((char *)mpd_song_get_uri(song));
    }

    return str;
}

int mpd_put_state(char *buffer, int *current_song_id, unsigned *queue_version)
{
    struct mpd_status *status;
    int len;

    status = mpd_run_status(mpd.conn);
    if (!status) {
        fprintf(stderr, "MPD mpd_run_status: %s\n", mpd_connection_get_error_message(mpd.conn));
        mpd.conn_state = MPD_FAILURE;
        return 0;
    }

    len = snprintf(buffer, MAX_SIZE,
        "{\"type\":\"state\", \"data\":{"
        " \"state\":%d, \"volume\":%d, \"repeat\":%d,"
        " \"single\":%d, \"crossfade\":%d, \"consume\":%d, \"random\":%d, "
        " \"songpos\": %d, \"elapsedTime\": %d, \"totalTime\":%d, "
        " \"currentsongid\": %d"
        "}}", 
        mpd_status_get_state(status),
        mpd_status_get_volume(status), 
        mpd_status_get_repeat(status),
        mpd_status_get_single(status),
        mpd_status_get_crossfade(status),
        mpd_status_get_consume(status),
        mpd_status_get_random(status),
        mpd_status_get_song_pos(status),
        mpd_status_get_elapsed_time(status),
        mpd_status_get_total_time(status),
        mpd_status_get_song_id(status));

    *current_song_id = mpd_status_get_song_id(status);
    *queue_version = mpd_status_get_queue_version(status);
    mpd_status_free(status);
    return len;
}

int mpd_put_outputs(char *buffer, int names)
{
    struct mpd_output *out;
    int nout;
    char *str, *strend;

    str = buffer;
    strend = buffer+MAX_SIZE;
    str += snprintf(str, strend-str, "{\"type\":\"%s\", \"data\":{",
            names ? "outputnames" : "outputs");

    mpd_send_outputs(mpd.conn);
    nout = 0;
    while ((out = mpd_recv_output(mpd.conn)) != NULL) {
        if (nout++)
            *str++ = ',';
        if (names)
            str += snprintf(str, strend - str, " \"%d\":\"%s\"",
                    mpd_output_get_id(out), mpd_output_get_name(out));
        else
            str += snprintf(str, strend-str, " \"%d\":%d",
                    mpd_output_get_id(out), mpd_output_get_enabled(out));
        mpd_output_free(out);
    }
    if (!mpd_response_finish(mpd.conn)) {
        fprintf(stderr, "MPD outputs: %s\n", mpd_connection_get_error_message(mpd.conn));
        mpd_connection_clear_error(mpd.conn);
        return 0;
    }
    str += snprintf(str, strend-str, " }}");
    return str-buffer;
}

int mpd_put_current_song(char *buffer)
{
    char *cur = buffer;
    const char *end = buffer + MAX_SIZE;
    struct mpd_song *song;
    struct t_meta data;

    song = mpd_run_current_song(mpd.conn);
    if(song == NULL)
        return 0;

    data.title = mpd_get_title(song);
    data.artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
    data.album = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
    data.track = mpd_song_get_tag(song, MPD_TAG_TRACK, 0);
	data.uri = mpd_song_get_uri(song);

    cur += json_emit_raw_str(cur, end - cur, "{\"type\": \"song_change\", \"data\":{\"pos\":");
    cur += json_emit_int(cur, end - cur, mpd_song_get_pos(song));
    cur += json_emit_raw_str(cur, end - cur, ",\"title\":");
    cur += json_emit_quoted_str(cur, end - cur, data.title);

    if(data.artist != NULL)
    {
        cur += json_emit_raw_str(cur, end - cur, ",\"artist\":");
        cur += json_emit_quoted_str(cur, end - cur, data.artist);
    }

    if(data.album != NULL)
    {
        cur += json_emit_raw_str(cur, end - cur, ",\"album\":");
        cur += json_emit_quoted_str(cur, end - cur, data.album);
    }

    if(template != 0) {
        cur += json_emit_raw_str(cur, end - cur, ",\"artwork\":");
        cur += json_emit_quoted_str(cur, end - cur, parse_templ(template, data));
    }

    cur += json_emit_raw_str(cur, end - cur, "}}");
    mpd_song_free(song);
    mpd_response_finish(mpd.conn);

    return cur - buffer;
}

int mpd_put_queue(char *buffer, unsigned int offset)
{
    char *cur = buffer;
    const char *end = buffer + MAX_SIZE;
    struct mpd_entity *entity;

    if (!mpd_send_list_queue_range_meta(mpd.conn, offset, offset+MAX_ELEMENTS_PER_PAGE))
        RETURN_ERROR_AND_RECOVER("mpd_send_list_queue_meta");

    cur += json_emit_raw_str(cur, end  - cur, "{\"type\":\"queue\",\"data\":[ ");

    while((entity = mpd_recv_entity(mpd.conn)) != NULL) {
        const struct mpd_song *song;

        if(mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG) {
            song = mpd_entity_get_song(entity);

            cur += json_emit_raw_str(cur, end - cur, "{\"id\":");
            cur += json_emit_int(cur, end - cur, mpd_song_get_id(song));
            cur += json_emit_raw_str(cur, end - cur, ",\"pos\":");
            cur += json_emit_int(cur, end - cur, mpd_song_get_pos(song));
            cur += json_emit_raw_str(cur, end - cur, ",\"duration\":");
            cur += json_emit_int(cur, end - cur, mpd_song_get_duration(song));
            cur += json_emit_raw_str(cur, end - cur, ",\"title\":");
            cur += json_emit_quoted_str(cur, end - cur, mpd_get_title(song));
			cur += json_emit_raw_str(cur, end - cur, ",\"artist\":");
			cur += json_emit_quoted_str(cur, end - cur, mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));
            cur += json_emit_raw_str(cur, end - cur, "},");
        }
        mpd_entity_free(entity);
    }

    /* remove last ',' */
    cur--;

    cur += json_emit_raw_str(cur, end - cur, "]}");
    return cur - buffer;
}

int mpd_put_browse(char *buffer, char *path, unsigned int offset)
{
    char *cur = buffer;
    const char *end = buffer + MAX_SIZE;
    struct mpd_entity *entity;
    unsigned int entity_count = 0;

	char quickStart = 0;
	char quickEnd;
	char quickList = 0;
	// Looking for [A-C]
	// special case [LST] for playlists only
	if(strlen(path) == 5 && *path == '[' && path[strlen(path) - 1] == ']') {
		quickStart = path[1];
		quickEnd = path[3];
		if(!strcmp("[LST]", path)) {
			quickList = 1;
		}
		strcpy(path, "/");
	}

    if (!mpd_send_list_meta(mpd.conn, path))
        RETURN_ERROR_AND_RECOVER("mpd_send_list_meta");

    cur += json_emit_raw_str(cur, end  - cur, "{\"type\":\"browse\",\"data\":[ ");

    while((entity = mpd_recv_entity(mpd.conn)) != NULL) {
        const struct mpd_song *song;
        const struct mpd_directory *dir;
        const struct mpd_playlist *pl;

		if(quickList) {
			if(mpd_entity_get_type(entity) != MPD_ENTITY_TYPE_PLAYLIST) {
				mpd_entity_free(entity);
				continue;
			}
		}
		else if(quickStart) {
			const char *name;

			switch (mpd_entity_get_type(entity)) {
				case MPD_ENTITY_TYPE_SONG:
					name = mpd_get_title(mpd_entity_get_song(entity));
					break;
				case MPD_ENTITY_TYPE_DIRECTORY:
					name = mpd_directory_get_path(mpd_entity_get_directory(entity));
					break;
				case MPD_ENTITY_TYPE_PLAYLIST:
					name = mpd_playlist_get_path(mpd_entity_get_playlist(entity));
					break;
				default:
					mpd_entity_free(entity);
					continue;
			}
			if(*name < quickStart || *name > quickEnd) {
				mpd_entity_free(entity);
				continue;
			}
		}

		if(offset > entity_count)
		{
			mpd_entity_free(entity);
			entity_count++;
			continue;
		}
		else if(offset + MAX_ELEMENTS_PER_PAGE - 1 < entity_count)
		{
			mpd_entity_free(entity);
			cur += json_emit_raw_str(cur, end  - cur, "{\"type\":\"wrap\",\"count\":");
			cur += json_emit_int(cur, end - cur, entity_count);
			cur += json_emit_raw_str(cur, end  - cur, "} ");
			break;
		}

        switch (mpd_entity_get_type(entity)) {
            case MPD_ENTITY_TYPE_UNKNOWN:
                break;

            case MPD_ENTITY_TYPE_SONG:
                song = mpd_entity_get_song(entity);
                cur += json_emit_raw_str(cur, end - cur, "{\"type\":\"song\",\"uri\":");
                cur += json_emit_quoted_str(cur, end - cur, mpd_song_get_uri(song));
                cur += json_emit_raw_str(cur, end - cur, ",\"duration\":");
                cur += json_emit_int(cur, end - cur, mpd_song_get_duration(song));
                cur += json_emit_raw_str(cur, end - cur, ",\"title\":");
                cur += json_emit_quoted_str(cur, end - cur, mpd_get_title(song));
                cur += json_emit_raw_str(cur, end - cur, "},");
                break;

            case MPD_ENTITY_TYPE_DIRECTORY:
                dir = mpd_entity_get_directory(entity);

                cur += json_emit_raw_str(cur, end - cur, "{\"type\":\"directory\",\"dir\":");
                cur += json_emit_quoted_str(cur, end - cur, mpd_directory_get_path(dir));
                cur += json_emit_raw_str(cur, end - cur, "},");
                break;

            case MPD_ENTITY_TYPE_PLAYLIST:
                pl = mpd_entity_get_playlist(entity);
                cur += json_emit_raw_str(cur, end - cur, "{\"type\":\"playlist\",\"plist\":");
                cur += json_emit_quoted_str(cur, end - cur, mpd_playlist_get_path(pl));
                cur += json_emit_raw_str(cur, end - cur, "},");
                break;
        }
        mpd_entity_free(entity);
        entity_count++;
    }

    if (mpd_connection_get_error(mpd.conn) != MPD_ERROR_SUCCESS || !mpd_response_finish(mpd.conn)) {
        fprintf(stderr, "MPD mpd_send_list_meta: %s\n", mpd_connection_get_error_message(mpd.conn));
        mpd.conn_state = MPD_FAILURE;
        return 0;
    }

    /* remove last ',' */
    cur--;

    cur += json_emit_raw_str(cur, end - cur, "]}");
    return cur - buffer;
}

int mpd_search(char *buffer, char *searchstr)
{
    int i = 0;
    char *cur = buffer;
    const char *end = buffer + MAX_SIZE;
    struct mpd_song *song;

    if(mpd_search_db_songs(mpd.conn, false) == false)
        RETURN_ERROR_AND_RECOVER("mpd_search_db_songs");
    else if(mpd_search_add_any_tag_constraint(mpd.conn, MPD_OPERATOR_DEFAULT, searchstr) == false)
        RETURN_ERROR_AND_RECOVER("mpd_search_add_any_tag_constraint");
    else if(mpd_search_commit(mpd.conn) == false)
        RETURN_ERROR_AND_RECOVER("mpd_search_commit");
    else {
        cur += json_emit_raw_str(cur, end - cur, "{\"type\":\"search\",\"data\":[ ");

        while((song = mpd_recv_song(mpd.conn)) != NULL) {
            cur += json_emit_raw_str(cur, end - cur, "{\"type\":\"song\",\"uri\":");
            cur += json_emit_quoted_str(cur, end - cur, mpd_song_get_uri(song));
            cur += json_emit_raw_str(cur, end - cur, ",\"duration\":");
            cur += json_emit_int(cur, end - cur, mpd_song_get_duration(song));
            cur += json_emit_raw_str(cur, end - cur, ",\"title\":");
            cur += json_emit_quoted_str(cur, end - cur, mpd_get_title(song));
			cur += json_emit_raw_str(cur, end - cur, ",\"artist\":");
			cur += json_emit_quoted_str(cur, end - cur, mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));
			cur += json_emit_raw_str(cur, end - cur, ",\"album\":");
			cur += json_emit_quoted_str(cur, end - cur, mpd_song_get_tag(song, MPD_TAG_ALBUM, 0));
            cur += json_emit_raw_str(cur, end - cur, "},");
            mpd_song_free(song);

            /* Maximum results */
            if(i++ >= 300)
            {
                cur += json_emit_raw_str(cur, end - cur, "{\"type\":\"wrap\"},");
                break;
            }
        }

        /* remove last ',' */
        cur--;

        cur += json_emit_raw_str(cur, end - cur, "]}");
    }
    return cur - buffer;
}


void mpd_disconnect()
{
    mpd.conn_state = MPD_DISCONNECT;
    mpd_poll(NULL);
}
