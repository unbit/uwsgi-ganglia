uwsgi-ganglia
=============

(WORK IN PROGRESS, only the stats pusher is implemented)

uWSGI plugin for ganglia integration

It implements:

* stats pusher

Building it
-----------

The plugin is 2.0-friendly so you can build it directly from your uWSGI binary:

```sh
uwsgi --build-plugin uwsgi-ganglia
```

since uWSGI 2.0.3 you can git-clone and build in one shot:

```
uwsgi --build-plugin https://github.com/unbit/uwsgi-ganglia
```

You can even build a uWSGI binary with the ganglia plugin embedded:

```
curl http://uwsgi.it/install | UWSGI_EMBED_PLUGINS="ganglia=https://github.com/unbit/uwsgi-ganglia" bash -s psgi /tmp/uwsgi
```

or (from the sources directory):

```
UWSGI_EMBED_PLUGINS="ganglia=https://github.com/unbit/uwsgi-ganglia" make
```

or (via pip)

```
UWSGI_EMBED_PLUGINS="ganglia=https://github.com/unbit/uwsgi-ganglia" pip install uwsgi
```

or (via gem)

```
UWSGI_EMBED_PLUGINS="ganglia=https://github.com/unbit/uwsgi-ganglia" gem install uwsgi
```

and so on...

Using it
========

Once loaded it register a stats-pusher, an alarm action and a hook.

For pushing stats to the ganglia server 239.2.11.71:8649

just use

```ini
[uwsgi]
plugin = ganglia
enable-metrics = true
stats-push = ganglia:239.2.11.71:8649
...
```
