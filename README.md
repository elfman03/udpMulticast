# udpMulticast

source inspired from https://gist.github.com/hostilefork/f7cae3dc33e7416f2dd25a402857b6c6

setup on openWRT:
  copy contents to /usr/local/udpMulticast/
  make
  make sure udpMulticast.loop and udpMulticast.service are executable
  modify udpMulticast.loop to reflect location of clients (ensure static IPs)
  modify udpMulticast.loop to reflect desired mode
  symlink udpMulticast.service into /etc/init.d/
  enable service in openwrt

  on clients, play (e.g., vlc "udp://@:7777" with 50ms network caching
