

GSOCKET works on Linux, FreeBSD, macOS, Cygwin and many others. If the particular OS is not listed here then try the *Install Script* or *compile from source*.

---
***Static Binary***

The [deploy instructions](https://www.gsocket.io/deploy/) are the fastest way to install gsocket. Some people say it's the **_World's smallest backdoor_**.

A stripped down version of just gs-netcat (without blitz, gs-mount, gs-ftp, ...) and no help (`--help`) is available as [pre-compiled static binary](https://github.com/hackerschoice/binary/tree/main/gsocket/bin).

---

**Lastest Source: https://github.com/hackerschoice/gsocket/releases/latest**  

---
**Generic - Compile from GitHub**
```
/bin/bash -c "$(curl -fsSL gsocket.io/install.sh)"
```
---
**Generic - Compile from Source**

Download the [latest source](https://github.com/hackerschoice/gsocket/releases/latest).
```
tar xfz gsocket-*.tar.gz
cd gsocket-*
./configure && make install
```
---
**Debian/Ubuntu/Kali**
```
apt update
apt install gsocket
```
---
**Other Linux**
```
curl -fsSL https://github.com/hackerschoice/binary/blob/main/gsocket/latest/gsocket_latest_all.deb --output gsocket_latest.deb
dpkg -i gsocket_latest.deb
```
---
**FreeBSD**
```
pkg update
pkg install gsocket
```
---
**Docker**

Try gsocket with Docker.
```
docker run --rm -it hackerschoice/gsocket
```
```
docker run --rm -it hackerschoice/gsocket-tor
```
---







