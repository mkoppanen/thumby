What is thumby?
===============

Thumby is a small server that literally thumbnails images. I might add more operations later
on but initially it's just thumbnailing. 


Dependencies
============

The dependencies are:

- ImageMagick
- libevent 2.x


Building and running
====================

At the moment the following command should do it:

    gcc -o thumby `pkg-config --cflags MagickWand` src/main.c `pkg-config --libs MagickWand` -levent -Wall

To run it do the following:

    ./thumby images/

To run on different port:

    ./thumby images/ 8811

The test images are created using the following commands:

    convert magick:rose images/rose.jpg
    convert magick:logo images/logo.png

Using
=====

Just access URL as follows:

    http://<hostname or ip>:<port>/thumb/rose.jpg?h=200w=500

width and height are optional.


Design
======

The design is not yet what I want it to be. At the moment there are multiple threads accepting
on a single fd, but I would much more prefer to have one thread accepting and pass the info to 
a worker thread over a lock-free queue. However, couldn't find an easy way to do this with evhttp.


TODO
====

A lot.