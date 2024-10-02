# M17 Digital Voice, portable fork

*M17 Digital Voice* , [mvoice](https://github.com/n7tae/mvoice), is developed by Tom Early (N7TAE) and it runs on Linux only. This fork, called *yamvoice*, is intended for running mvoice on non-Linux environment, especially OpenBSD.

**Note: OpenBSD support is now work-in-progress.**

## Building tools and prerequisites

At least, CMake, FLTK, libcurl, libintl, and library for audio API (libsndio or libasound) is required. OpenDHT is optional.

## Building and Installing *yamvoice*

### Get the *yamvoice* repository, and move to it

```bash
git clone https://github.com/jg1uaa/yamvoice.git
cd yamvoice
mkdir build
cd build
```

### Compiling

Normally there is no need to configuration like this:

```
cmake ..
```

And build:

```bash
make
```

You can use following options for cmake, like `cmake -DAUDIO=sndio ..` .

<dl>
 <dt><code>AUDIO</code>
 <dd>Select audio API, <code>sndio</code> (OpenBSD default) or <code>alsa</code> (Linux default).
 <dt><code>BASEDIR</code>
 <dd>Base directory for install (like --prefix of configure), default <code>/usr/local</code> .
 <dt><code>CFGDIR</code>
 <dd>Directory that stores user configuration files, default <code>.config/yamvoice</code> .
 <dt><code>USE44100</code>
 <dd>If your audio device does not support 8000Hz sampling rate (but 44100Hz is supported), set <code>ON</code> . Otherwise (default) <code>OFF</code> .
 <dt><code>DISABLE_OPENDHT</code>
 <dd>OpenDHT is automatically detected and enabled if available. OpenDHT is installed but you do not want to use it, set <code>ON</code>. Otherwise (default) <code>OFF</code> .
 <dt><code>DEBUG</code>
 <dd><code>ON</code> enables build with gdb debug support, default <code>OFF</code> .
</dl>

Thanks for Tom/N7TAE who wrote significant application for M17 world.

de JG1UAA <uaa@uaa.org.uk>
