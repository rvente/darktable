/*
    This file is part of darktable,
    copyright (c) 2011 henrik andersson

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <glib.h>
#include <inttypes.h>
#include "common/gpx.h"
#include "common/darktable.h"

typedef struct _gpx_track_point_t {
  gdouble longitude, latitude, elevation;
  GTimeVal time;
} _gpx_track_point_t;

typedef struct dt_gpx_t {
  /* the list of track records parsed */
  GList *track;

  /* currently parsed track point */
  _gpx_track_point_t *current_track_point;
  uint32_t current_parser_element;
  gboolean invalid_track_point;

} dt_gpx_t;



/* GPX XML parser */
#define GPX_PARSER_ELEMENT_TRKPT   1
#define GPX_PARSER_ELEMENT_TIME    2
#define GPX_PARSER_ELEMENT_ELE     4


static void _gpx_parser_start_element(GMarkupParseContext *ctx,
				      const gchar *element_name, const gchar **attribute_names,
				      const gchar **attribute_values, gpointer ueer_data,
				      GError **error);
static void _gpx_parser_end_element(GMarkupParseContext *context, const gchar *element_name,
				    gpointer user_data, GError **error);
static void _gpx_parser_text(GMarkupParseContext *context,
			     const gchar *text, gsize text_len,
			     gpointer user_data,GError **error);

static GMarkupParser _gpx_parser = {
  _gpx_parser_start_element,
  _gpx_parser_end_element,
  _gpx_parser_text,
  NULL,
  NULL
};


dt_gpx_t *dt_gpx_new(const gchar *filename)
{
  dt_gpx_t *gpx = NULL;
  GMarkupParseContext *ctx = NULL;
  GError *err = NULL;
  GMappedFile *gpxmf = NULL;
  gchar *gpxmf_content = NULL;
  gint gpxmf_size = 0;


  /* map gpx file to parse into memory */
  gpxmf = g_mapped_file_new(filename, FALSE, &err);
  if (err)
    goto error;

  gpxmf_content = g_mapped_file_get_contents(gpxmf);
  gpxmf_size = g_mapped_file_get_length(gpxmf);
  if (!gpxmf_content || gpxmf_size < 10)
    goto error;
  
  /* allocate new dt_gpx_t context */
  gpx = g_malloc(sizeof(dt_gpx_t));
  memset(gpx, 0, sizeof(dt_gpx_t));

  /* initialize the parser and start parse gpx xml data */
  ctx = g_markup_parse_context_new(&_gpx_parser, 0, gpx, NULL);
  g_markup_parse_context_parse(ctx, gpxmf_content, gpxmf_size, &err);
  if (err)
    goto error;


  /* clenup and return gpx context */
  g_markup_parse_context_free(ctx);

  return gpx;

error:
  if (err)
  {
    fprintf(stderr, "dt_gpx_new: %s\n", err->message);
    g_error_free(err);
  }

  if (ctx)
      g_markup_parse_context_free(ctx);

  if (gpx) 
    g_free(gpx);

  return NULL;
}

void dt_gpx_destroy(struct dt_gpx_t *gpx)
{
  g_assert(gpx != NULL);

  if (gpx->track)
    g_list_foreach(gpx->track, (GFunc)g_free, NULL);
  g_free(gpx);
}

gboolean dt_gpx_get_location(struct dt_gpx_t *gpx, GTimeVal *timestamp, gdouble *lon, gdouble *lat)
{
  g_assert(gpx != NULL);
  
  GList *item = g_list_first(gpx->track);
  
  /* verify that we got at least 2 trackpoints */
  if (!item || !item->next)
    return FALSE;

  do {

    _gpx_track_point_t *tp = (_gpx_track_point_t *)item->data;

    fprintf(stderr,"Comparing %ld with %ld (diff = %ld)\n",timestamp->tv_sec, tp->time.tv_sec,
	    timestamp->tv_sec - tp->time.tv_sec );

    /* if timestamp is out of time range return false but fill
       closest location value */
    if ((!item->next && timestamp->tv_sec >= tp->time.tv_sec)
	|| (timestamp->tv_sec <= tp->time.tv_sec))
    {
      *lon = tp->longitude;
      *lat = tp->latitude;
      return FALSE;
    }

    /* check if timestamp is within current and next trackpoint */
    if (timestamp->tv_sec >= tp->time.tv_sec &&
	timestamp->tv_sec <= ((_gpx_track_point_t*)item->next->data)->time.tv_sec)
    {
      *lon = tp->longitude;
      *lat = tp->latitude;
      return TRUE;
    }

  } while ((item = g_list_next(item)) != NULL);
  
  /* should not reach this point */
  return FALSE;
}

/* 
 * GPX XML parser code 
 */
void _gpx_parser_start_element(GMarkupParseContext *ctx,
				      const gchar *element_name, const gchar **attribute_names,
				      const gchar **attribute_values, gpointer user_data,
				      GError **error)
{
  dt_gpx_t *gpx = (dt_gpx_t *)user_data;

  if (strcmp(element_name, "trkpt") == 0) 
  {
    if (gpx->current_track_point)
    {
      fprintf(stderr,"broken gpx file, new trkpt element before the previous ended.\n");
      g_free(gpx->current_track_point);
    }

    const gchar **attribute_name = attribute_names;
    const gchar **attribute_value = attribute_values;

    gpx->invalid_track_point = FALSE;
    
    if (*attribute_name)
    {
      gpx->current_track_point = g_malloc(sizeof(_gpx_track_point_t));
      memset(gpx->current_track_point, 0, sizeof(_gpx_track_point_t));

      /* initialize with NAN for validation check */
      gpx->current_track_point->longitude = NAN;
      gpx->current_track_point->latitude = NAN;

      /* go thru the attributes to find and get values of lon / lat*/
      while (*attribute_name)
      {
	if (strcmp(*attribute_name, "lon") == 0)
	  gpx->current_track_point->longitude =  g_ascii_strtod(*attribute_value, NULL);
	else if(strcmp(*attribute_name, "lat") == 0)
	  gpx->current_track_point->latitude = g_ascii_strtod(*attribute_value, NULL);
	
	attribute_name++;
	attribute_value++;
      }

      /* validate that we actually got lon / lat attribute values */
      if (gpx->current_track_point->longitude == NAN || 
	  gpx->current_track_point->latitude == NAN)
      {
	fprintf(stderr,"broken gpx file, failed to get lon/lat attribute values for trkpt\n");
	gpx->invalid_track_point = TRUE;
      }

    }
    else
      fprintf(stderr,"broken gpx file, trkpt element doesnt have lon/lat attributes\n");

    gpx->current_parser_element = GPX_PARSER_ELEMENT_TRKPT;
  }
  else if (strcmp(element_name, "time") == 0)
  {
    if (!gpx->current_track_point)
      goto element_error;

    gpx->current_parser_element = GPX_PARSER_ELEMENT_TIME;
  }
  else if (strcmp(element_name, "ele") == 0)
  {
    if (!gpx->current_track_point)
      goto element_error;
    
    gpx->current_parser_element = GPX_PARSER_ELEMENT_ELE;
  }

  return;

element_error:
  fprintf(stderr, "broken gpx file, element '%s' found outside of trkpt.\n",
	  element_name);
 
}

void _gpx_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
				     gpointer user_data, GError **error)
{
  dt_gpx_t *gpx = (dt_gpx_t *)user_data;

  /* closing trackpoint lets take care of data parsed */
  if (strcmp(element_name, "trkpt") == 0)
  {
    if (!gpx->invalid_track_point)
      gpx->track = g_list_append(gpx->track, gpx->current_track_point);
    else
      g_free(gpx->current_track_point);

    gpx->current_track_point = NULL;
  }

  /* clear current parser element */
  gpx->current_parser_element = 0;

}

void _gpx_parser_text(GMarkupParseContext *context,
			     const gchar *text, gsize text_len,
			     gpointer user_data,GError **error)
{
  dt_gpx_t *gpx = (dt_gpx_t *)user_data;

  if (!gpx->current_track_point)
    return;

  if (gpx->current_parser_element == GPX_PARSER_ELEMENT_TIME)
  {
    
    if (!g_time_val_from_iso8601(text, &gpx->current_track_point->time)) 
    {
      gpx->invalid_track_point = TRUE;
      fprintf(stderr,"broken gpx file, failed to pars is8601 time '%s' for trackpoint\n",text);
    }
  }
  else if (gpx->current_parser_element == GPX_PARSER_ELEMENT_ELE)
    gpx->current_track_point->elevation = g_ascii_strtod(text, NULL);

}
