#!/usr/bin/python
# Software License Agreement (BSD License)
#
# Copyright (C) 2012, Jack O'Quin
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above
#    copyright notice, this list of conditions and the following
#    disclaimer in the documentation and/or other materials provided
#    with the distribution.
#  * Neither the name of the author nor of other contributors may be
#    used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# Revision $Id$

"""
Plan the shortest route through a geographic information network.

:todo: add server for local GetPlan service.
"""

PKG_NAME = 'route_network'
import roslib; roslib.load_manifest(PKG_NAME)
import rospy
import sys

from route_network import planner

from geographic_msgs.msg import RouteNetwork
from geographic_msgs.msg import RoutePath
from geographic_msgs.srv import GetRoutePlan
from geographic_msgs.srv import GetRoutePlanResponse

class RoutePlannerNode():

    def __init__(self):
        """ROS node to provide a navigation plan graph for a RouteNetwork.
        """
        rospy.init_node('route_planner')
        self.graph = None
        self.planner = None

        # advertise route planning service
        self.srv = rospy.Service('get_route_plan', GetRoutePlan,
                                 self.route_planner)
        self.resp = None

        # subscribe to route network
        self.sub = rospy.Subscriber('route_network', RouteNetwork,
                                    self.graph_callback)

    def graph_callback(self, graph):
        """ RouteNetwork graph message callback.
        :param graph: RouteNetwork message

        :post: graph information saved in self.graph and self.planner
        """
        self.graph = graph
        self.planner = planner.Planner(graph)

    def route_planner(self, req):
        """
        :param req: GetRoutePlanRequest message
        :returns: GetRoutePlanResponse message
        """
        self.resp = GetRoutePlanResponse(plan = RoutePath())
        if self.graph is None:
            self.resp.success = False
            self.resp.status = 'no RouteNetwork available for GetRoutePlan'
            rospy.logerr(self.resp.status)
            return self.resp

        try:
            # plan a path to the goal
            self.resp.plan = self.planner.planner(req)
        except (ValueError, planner.NoPathToGoalError) as e:
            self.resp.success = False
            self.resp.status = e
            rospy.logerr('route planner exception: ' + e)
        else:
            self.resp.success = True
            self.resp.plan.header.stamp = rospy.Time.now()
            self.resp.plan.header.frame_id = self.graph.header.frame_id
        return self.resp

def main():
    node_class = RoutePlannerNode()
    try:
        rospy.spin()            # wait for messages
    except rospy.ROSInterruptException: pass
    return 0

if __name__ == '__main__':
    # run main function and exit
    sys.exit(main())
