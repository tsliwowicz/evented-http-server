evented-http-server
===================

A very fast multithreaded http server based on libevent (for cross platform async IO) and apr (for cross platform thread pooling) with a very simple API. Current version compiles on Linux (tested on Ubuntu 12.04 64 bit), but OSX and Windows build files and support will be added as soon as I have time.

I 

Dependencies
============
libevent (I used https://github.com/downloads/libevent/libevent/libevent-2.0.21-stable.tar.gz)
apr (APR 1.4.6, APR-util 1.5.2 - http://apr.apache.org/)

In ubuntu you can install them as following;
sudo apt-get install libevent-dev libapr1-dev

BUILD 
=====

cd into either the Release or the Debug subdirectories and run
make

License
=======

Copyright 2013 Tal Sliwowicz

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.


