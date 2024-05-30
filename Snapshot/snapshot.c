#include <gst/gst.h>
#include <string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>

#define CHUNK_SIZE 1024         /* Amount of bytes we are sending in each buffer */
#define SAMPLE_RATE 44100       /* Samples per second we are sending */

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData
{
  GstElement *pipeline, *video_source, *rtph_depay, *av_dec, *video_convert, *tee, *app_queue, *video_queue, *app_sink, *video_sink;
  gboolean snapshot;
  GMainLoop *main_loop;         /* GLib's Main Loop */

} CustomData;


/* Process keyboard input */
static gboolean
handle_keyboard (GIOChannel * source, GIOCondition cond, CustomData * data)
{
  gchar *str = NULL;

  if (g_io_channel_read_line (source, &str, NULL, NULL,
          NULL) != G_IO_STATUS_NORMAL) {
    return TRUE;
  }

  switch (g_ascii_tolower (str[0])) {
    case 's':
      data->snapshot = TRUE;
    default:
      break;
  }

  g_free (str);

  return TRUE;
}

/* The appsink has received a buffer */
static GstFlowReturn new_sample (GstElement * sink, CustomData * data)
{
  if(data->snapshot){
    GstSample *sample;
    /* Retrieve the buffer */
    g_signal_emit_by_name (sink, "pull-sample", &sample);

    if (sample) {
      /* The only thing we do in this example is print a * to indicate a received buffer */
      g_print ("New Sample \n");

      GstBuffer *buffer;
      GstCaps *caps;
      GstStructure *s;
      gboolean res;
      gint width, height;
      GError *error = NULL;
      GstMapInfo map;
      GdkPixbuf *pixbuf;

      caps = gst_sample_get_caps (sample);
      if (!caps) {
        g_print ("could not get snapshot format\n");
        exit (-1);
      }
      s = gst_caps_get_structure (caps, 0);
      
      /* we need to get the final caps on the buffer to get the size */
      res = gst_structure_get_int (s, "width", &width);
      res |= gst_structure_get_int (s, "height", &height);
      if (!res) {
        g_print ("could not get snapshot dimension\n");
        exit (-1);
      }

      /* create pixmap from buffer and save, gstreamer video buffers have a stride
      * that is rounded up to the nearest multiple of 4 */
      buffer = gst_sample_get_buffer (sample);
      gst_buffer_map (buffer, &map, GST_MAP_READ);
      pixbuf = gdk_pixbuf_new_from_data (map.data,
          GDK_COLORSPACE_RGB, FALSE, 8, width, height,
          GST_ROUND_UP_4 (width * 3), NULL, NULL);

      /* save the pixbuf */
      gdk_pixbuf_save (pixbuf, "snapshot.jpg", "jpeg", &error, NULL);
      gst_buffer_unmap (buffer, &map);

      gst_sample_unref (sample);
      data->snapshot = FALSE;
      return GST_FLOW_OK;
    }
    return GST_FLOW_ERROR;
  } else {
    return GST_FLOW_OK;
  }
}
  
int tutorial_main (int argc, char *argv[])
{
  CustomData data;
  GstPad *tee_video_pad, *tee_app_pad;
  GstPad *queue_video_pad, *queue_app_pad;
  data.snapshot = FALSE;
  GIOChannel *io_stdin;
  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  data.video_source = gst_element_factory_make ("udpsrc", "video_source");
  data.rtph_depay = gst_element_factory_make( "rtph264depay", "rtph_depay");
  data.av_dec = gst_element_factory_make ("avdec_h264", "av_dec");
  data.video_convert = gst_element_factory_make ("videoconvert", "video_convert");
  data.tee = gst_element_factory_make("tee", "tee");
  data.video_queue = gst_element_factory_make("queue", "video_queue");
  data.app_queue = gst_element_factory_make("queue", "app_queue");
  data.video_sink = gst_element_factory_make("autovideosink", "video_sink");
  data.app_sink = gst_element_factory_make ("appsink", "app_sink");

  /* Create the empty pipeline */
  data.pipeline = gst_pipeline_new ("test-pipeline");

  if (!data.pipeline || !data.video_source || !data.rtph_depay || !data.av_dec || !data.video_convert || !data.app_sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Configure udpsrc */
  g_object_set (data.video_source, "uri", "udp://localhost:30120", NULL);
  g_object_set (G_OBJECT(data.video_source), "caps", gst_caps_new_simple ("application/x-rtp",  "media", G_TYPE_STRING, "video", "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "H264", "payload", G_TYPE_INT, 96,  NULL), NULL);

  /* Configure appsink */
  g_object_set (data.app_sink, "emit-signals", TRUE, NULL);
  g_signal_connect (data.app_sink, "new-sample", G_CALLBACK (new_sample), &data);


  /* Link all elements that can be automatically linked because they have "Always" pads */
  gst_bin_add_many (GST_BIN (data.pipeline), data.video_source, data.rtph_depay, data.av_dec, data.video_convert, data.tee,
                             data.app_queue, data.video_queue, data.app_sink, data.video_sink, NULL);
                             
  if (gst_element_link_many (data.video_source, data.rtph_depay, data.av_dec, data.video_convert, data.tee, NULL) != TRUE
      || gst_element_link_many (data.video_queue, data.video_sink, NULL) != TRUE
      || gst_element_link_many (data.app_queue, data.app_sink, NULL) != TRUE) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  /* Manually link the Tee, which has "Request" pads */
  tee_video_pad = gst_element_request_pad_simple (data.tee, "src_%u");
  g_print ("Obtained request pad %s for video branch.\n",
      gst_pad_get_name (tee_video_pad));
  queue_video_pad = gst_element_get_static_pad (data.video_queue, "sink");
  tee_app_pad = gst_element_request_pad_simple (data.tee, "src_%u");
  g_print ("Obtained request pad %s for app branch.\n",
      gst_pad_get_name (tee_app_pad));
  queue_app_pad = gst_element_get_static_pad (data.app_queue, "sink");
  if (gst_pad_link (tee_video_pad, queue_video_pad) != GST_PAD_LINK_OK ||
      gst_pad_link (tee_app_pad, queue_app_pad) != GST_PAD_LINK_OK) {
    g_printerr ("Tee could not be linked\n");
    gst_object_unref (data.pipeline);
    return -1;
  }
  gst_object_unref (queue_video_pad);
  gst_object_unref (queue_app_pad);


  io_stdin = g_io_channel_unix_new (fileno (stdin));
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc) handle_keyboard, &data);

  /* Start playing the pipeline */
  gst_element_set_state (data.pipeline, GST_STATE_PLAYING);

  /* Create a GLib Main Loop and set it to run */
  data.main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.main_loop);

  /* Free resources */
  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);
  return 0;

}

int
main (int argc, char *argv[])
{
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  return gst_macos_main ((GstMainFunc) tutorial_main, argc, argv, NULL);
#else
  return tutorial_main (argc, argv);
#endif
}