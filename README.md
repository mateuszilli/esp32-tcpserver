# esp32-tcpserver

## Dependencies

- [esp-id](https://github.com/espressif/esp-idf)
- [esp-idf-lib](https://github.com/UncleRus/esp-idf-lib.git)

## Initial settings

After the dependencies are installed and configured, you must change the following variables in your [sdkconfig](./sdkconfig) file according to your wifi configuration:
- `CONFIG_EXAMPLE_WIFI_SSID="CONFIG_EXAMPLE_WIFI_SSID"`
- `CONFIG_EXAMPLE_WIFI_PASSWORD="CONFIG_EXAMPLE_WIFI_PASSWORD"`

## How to test

After compiling and loading the code in esp32 you must open a tcp client connection on port 3333 and the ip that will be printed on the serial monitor.

Software suggestions for making the tcp connection:
- In Linux you can use [netcat](http://netcat.sourceforge.net).
- In Windows you can use the [realterm](https://realterm.sourceforge.io/).