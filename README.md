### Dependencies

GNU Radio v3.7.

packets from your packet manager:
```
sudo apt-get install cmake libboost-all-dev liblog4cpp5-dev swig
```


### Installation

```
mkdir build
cd build
cmake ..
make
sudo make install
sudo ldconfig
```


### Usage

open apps/rds_rx.grc example flow graph in GNU Radio Companion.


to change the rds values you need a tcp client.

`nc 127.0.0.1 52001`

set a value with
`key value`

### TCP Commands
```
status:
  disabled
pty:
  @param int programm type
  https://en.wikipedia.org/wiki/Radio_Data_System#Program_types
text:
  @param string
  radio test
ps:
  @param
ta:
  @param bool
tp:
  @param bool
ms:
  @param bool
pi:
  @param 
af1:
  @param double
af2:
  @param double
```

### Demos

Quick example:
http://www.youtube.com/watch?v=05i9C5lhorY

HAK5 episode (including installation):
http://www.youtube.com/watch?v=ukhrIl4JHbw


### History

Continuation of gr-rds on BitBucket (originally from Dimitrios Symeonidis https://bitbucket.org/azimout/gr-rds/ and also on CGRAN https://www.cgran.org/wiki/RDS).
