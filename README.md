# GStreamerProjects

```console
~$ gst-launch-1.0 videotestsrc ! videorate ! video/x-raw,width=720,height=576,framerate=25/1 ! x264enc ! rtph264pay ! udpsink port=30120 host=localhost
```
