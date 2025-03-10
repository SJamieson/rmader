#!/usr/bin/env python  
# Kota Kondo

#  Commands: Open three terminals and execute this:
#  python collision_check.py

#Whenever you want, you can ctrl+C the second command and you will get the absolute and relative velocities

import bagpy
from bagpy import bagreader
import pandas as pd
import sys
import rosbag
import rospy
import math
import tf2_ros
import geometry_msgs.msg
import tf
import numpy as np
import matplotlib.pyplot as plt
import os
import glob

if __name__ == '__main__':

    ##### parameters
    cd_list = [50]

    ##### loop
    for cd in cd_list:
        is_oldmader=False
        if cd == 0: 
            dc_list = [75] #dc_list[0] will be used for old mader (which doesn't need delay check) so enter some value (default 0)
        elif cd == 50:
            dc_list = [80] #dc_list[0] will be used for old mader (which doesn't need delay check) so enter some value (default 0)
        elif cd == 100:
            dc_list = [175] #dc_list[0] will be used for old mader (which doesn't need delay check) so enter some value (default 0)
        elif cd == 200:
            dc_list = [250]
        elif cd == 300:
            dc_list = [350]

        for dc in dc_list:
            collision_cnt = 0
            str_dc = str(dc)

            ##### source directory
            parent_source_dir = sys.argv[1]
            if is_oldmader:
                source_dir = parent_source_dir + "/oldmader/bags/cd"+str(cd)+"ms" # change the source dir accordingly #10 agents
            else:
                source_dir = parent_source_dir + "/rmader_obs/bags/cd"+str(cd)+"ms/dc"+str_dc+"ms" # change the source dir accordingly #10 agents
            
            source_len = len(source_dir)
            source_bags = source_dir + "/*.bag" # change the source dir accordingly

            rosbag_list = glob.glob(source_bags)
            rosbag_list.sort() #alphabetically order
            rosbag = []

            for bag in rosbag_list:
                rosbag.append(bag)

            for i in range(len(rosbag)):

                try:
                    b = bagreader(rosbag[i], verbose=False)
                    sim_id = rosbag[i][source_len+5:source_len+8]
                    log_data = b.message_by_topic("/is_collided")
                    if (log_data == None):
                        print("sim " + sim_id + ": no collision" )
                        os.system('echo "simulation '+sim_id+': no collision" >> '+source_dir+'/collision_status.txt')
                    else:
                        collision_cnt = collision_cnt + 1
                        print("sim " + sim_id + ": ******collision******" )
                        os.system('echo "simulation '+sim_id+': ***collision***" >> '+source_dir+'/collision_status.txt')
                except:
                    pass

            collision_per = 100 - collision_cnt / len(rosbag) * 100
            os.system('paste '+source_dir+'/collision_status.txt '+source_dir+'/status.txt >> '+source_dir+'/complete_status.txt')
            os.system('echo "'+source_dir+'" >> '+parent_source_dir+'/collision_count.txt')
            os.system('echo "'+str(collision_cnt)+'/'+str(len(rosbag))+' - '+str(round(collision_per,2))+'%" >> '+parent_source_dir+'/collision_count.txt')
            os.system('echo "------------------------------------------------------------" >> '+parent_source_dir+'/collision_count.txt')

            # is_oldmader = False