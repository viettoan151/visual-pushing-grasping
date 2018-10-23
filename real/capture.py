#!/usr/bin/env python

import time
import matplotlib.pyplot as plt
from camera import Camera

camera = Camera()
time.sleep(1) # Give camera some time to load data
count = 0
delaytime = 5 #seconds delay
while True:
    print('Take picture {}'.format(count))
    color_img, depth_img = camera.get_data()
    plt.subplot(211)
    plt.imshow(color_img)
    plt.subplot(212)
    plt.imshow(depth_img)
    plt.show()
    for i in range(delaytime):
        print('{}'.format(delaytime - i))
        time.sleep(1)
    count = count + 1
