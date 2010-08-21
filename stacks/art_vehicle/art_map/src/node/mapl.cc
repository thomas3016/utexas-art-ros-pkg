/*
 * ROS node for ART map lanes.
 *
 *  Copyright (C) 2005 Austin Robot Technology
 *  Copyright (C) 2010 Austin Robot Technology
 *
 *  License: Modified BSD Software License Agreement
 *
 *  $Id$
 */

#include <unistd.h>
#include <string.h>
#include <iostream>

#include <ros/ros.h>
#include <tf/tf.h>

#include <art_common/ArtHertz.h>
#include <nav_msgs/Odometry.h>
#include <visualization_msgs/MarkerArray.h>

#include <art_map/ArtLanes.h>
#include <art_map/Graph.h>
#include <art_map/MapLanes.h>
#include <art_map/RNDF.h>


/** @file

 @brief provide RNDF map lane boundaries for ART vehicle

Subscribes:

- @b odom [nav_msgs::Odometry] estimate of robot position and velocity.

Publishes:

- @b roadmap_global [art_map::ArtLanes] global road map lanes (latched topic)
- @b roadmap_local [art_map::ArtLanes] local area road map lanes
- @b visualization_marker_array [visualization_msgs::MarkerArray]
     markers for map visualization

@author Jack O'Quin, Patrick Beeson

*/

/** ROS node class for road map driver. */
class MapLanesDriver
{
public:
    
  // Constructor
  MapLanesDriver();
  ~MapLanesDriver()
    {
      delete map_;
      if (graph_)
        delete graph_;
    };

  bool buildRoadMap(void);
  int  Setup(ros::NodeHandle node);
  int  Shutdown(void);
  void Spin(void);

private:

  void processOdom(const nav_msgs::Odometry::ConstPtr &odomIn);
  void publishGlobalMap(void);
  void publishLocalMap(void);
  void publishMapMarks(ros::Publisher &pub,
                       const std::string &map_name,
                       ros::Duration life,
                       const art_map::ArtLanes &lane_data);

  // parameters:
  double range_;                ///< radius of local lanes to report (m)
  double poly_size_;            ///< maximum polygon size (m)
  std::string rndf_name_;       ///< Road Network Definition File name
  std::string frame_id_;        ///< frame ID of map (default "/map")

  // topics and messages
  ros::Subscriber odom_topic_;       // odometry topic
  nav_msgs::Odometry odom_msg_;      // last Odometry message received

  ros::Publisher roadmap_global_;       // global road map publisher
  ros::Publisher roadmap_local_;        // local road map publisher
  ros::Publisher mapmarks_;             // rviz visualization markers

  // this vector is only used while publishMapMarks() is running
  // we define it here to avoid memory allocation on every cycle
  visualization_msgs::MarkerArray marks_msg_;

  Graph *graph_;                  ///< graph object (used by MapLanes)
  MapLanes* map_;                 ///< MapLanes object instance
  bool initial_position_;         ///< true if initial odometry received
};

/** constructor */
MapLanesDriver::MapLanesDriver(void)
{
  initial_position_ = false;

  // use private node handle to get parameters
  ros::NodeHandle nh("~");

  frame_id_ = "/map";
  if (nh.getParam("frame_id", frame_id_))
    {
      ROS_INFO_STREAM("map frame ID = " << frame_id_);
    }

  nh.param("range", range_, 80.0);
  ROS_INFO("range to publish = %.0f meters", range_);

  nh.param("poly_size", poly_size_, MIN_POLY_SIZE);
  ROS_INFO("polygon size = %.0f meters", poly_size_);

  rndf_name_ = "";
  std::string rndf_param;
  if (nh.searchParam("rndf", rndf_param))
    {
      nh.param(rndf_param, rndf_name_, std::string(""));
      ROS_INFO_STREAM("RNDF: " << rndf_name_);
    }
  else
    {
      ROS_ERROR("RNDF not defined");
    }

  // create the MapLanes class
  map_ = new MapLanes(range_);
  graph_ = NULL;
}

/** Set up ROS topics */
int MapLanesDriver::Setup(ros::NodeHandle node)
{   
  static int qDepth = 1;
  ros::TransportHints noDelay = ros::TransportHints().tcpNoDelay(true);

  odom_topic_ = node.subscribe("odom", qDepth, &MapLanesDriver::processOdom,
                               this, noDelay);

  // Local road map publisher
  roadmap_local_ =
    node.advertise<art_map::ArtLanes>("roadmap_local", qDepth);

  // Use latched publisher for global road map and visualization topics
  roadmap_global_ =
    node.advertise<art_map::ArtLanes>("roadmap_global", 1, true);
  mapmarks_ = node.advertise <visualization_msgs::MarkerArray>
    ("visualization_marker_array", 1);

  return 0;
}


/** Shutdown the driver */
int MapLanesDriver::Shutdown()
{
  // Stop and join the driver thread
  ROS_INFO("shutting down maplanes");
  return 0;
}

/** Handle odometry input */
void MapLanesDriver::processOdom(const nav_msgs::Odometry::ConstPtr &odomIn)
{
  odom_msg_ = *odomIn;
  if (initial_position_ == false)
    {
      ROS_INFO("initial odometry received");
      initial_position_ = true;         // have position data now
    }
}

/** @brief Publish map visualization markers
 *
 *  Converts polygon data into an array of rviz visualization
 *  markers.
 *
 *  @param pub topic to publish
 *  @param map_name marker namespace
 *  @param life lifespan for these markers
 *  @param lane_data polygons to publish
 */
void MapLanesDriver::publishMapMarks(ros::Publisher &pub,
                                     const std::string &map_name,
                                     ros::Duration life,
                                     const art_map::ArtLanes &lane_data)
{
  if (pub.getNumSubscribers() == 0)     // no subscribers?
    return;

  std_msgs::ColorRGBA green;            // green map markers
  green.r = 0.0;
  green.g = 1.0;
  green.b = 0.0;
  green.a = 1.0;
  ros::Time now = ros::Time::now();

  marks_msg_.markers.clear();
  for (uint32_t i = 0; i < lane_data.polygons.size(); ++i)
    {
      visualization_msgs::Marker mark;
      mark.header.stamp = now;
      mark.header.frame_id = frame_id_;

      // publish polygon centers
      mark.ns = "polygons_" + map_name;
      mark.id = (int32_t) i;
      mark.type = visualization_msgs::Marker::ARROW;
      mark.action = visualization_msgs::Marker::ADD;

      mark.pose.position = lane_data.polygons[i].midpoint;
      mark.pose.orientation = 
        tf::createQuaternionMsgFromYaw(lane_data.polygons[i].heading);

      mark.scale.x = 1.0;
      mark.scale.y = 1.0;
      mark.scale.z = 1.0;
      mark.color = green;
      mark.lifetime = life;

      // Add this polygon to the vector of markers to publish
      marks_msg_.markers.push_back(mark);

      if (!lane_data.polygons[i].is_transition)
        {
          visualization_msgs::Marker lane;
          lane.header.stamp = now;
          lane.header.frame_id = frame_id_;

          // publish lane boundaries (experimental)
          //
          // It is almost certainly more efficient for rviz rendering
          // to collect the right and left lane boundaries for each
          // lane as two strips, then publish them as separate
          // LINE_STRIP markers. This LINE_LIST version is an
          // experiment to see how it looks (pretty good).
          lane.ns = "lanes_" + map_name ;
          lane.id = (int32_t) i;
          lane.type = visualization_msgs::Marker::LINE_LIST;
          lane.action = visualization_msgs::Marker::ADD;

          // define lane boundary points: first left (0, 1), then right (2, 3)
          for (uint32_t j = 0;
               j < lane_data.polygons[i].poly.points.size(); ++j)
            {
              // convert Point32 message to Point (there should be a
              // better way)
              geometry_msgs::Point p;
              p.x = lane_data.polygons[i].poly.points[j].x;
              p.y = lane_data.polygons[i].poly.points[j].y;
              p.z = lane_data.polygons[i].poly.points[j].z;
              lane.points.push_back(p);
            }

          lane.scale.x = 0.1;               // 10cm lane boundaries
          lane.color = green;
          lane.lifetime = life;

          // Add these boundaries to the vector of markers to publish
          marks_msg_.markers.push_back(lane);
        }

      if (lane_data.polygons[i].contains_way)
        {
          visualization_msgs::Marker wp;
          wp.header.stamp = now;
          wp.header.frame_id = frame_id_;

          // publish way-points
          wp.ns = "waypoints_" + map_name;
          wp.id = (int32_t) i;
          wp.type = visualization_msgs::Marker::CYLINDER;
          wp.action = visualization_msgs::Marker::ADD;

          wp.pose.position = lane_data.polygons[i].midpoint;
          wp.pose.orientation = 
            tf::createQuaternionMsgFromYaw(lane_data.polygons[i].heading);

          wp.scale.x = 1.0;
          wp.scale.y = 1.0;
          wp.scale.z = 0.1;
          wp.lifetime = life;

          wp.color.a = 0.8;     // way-points are slightly transparent
          if (lane_data.polygons[i].is_stop)
            {
              // make stop way-points red
              wp.color.r = 1.0;
              wp.color.g = 0.0;
              wp.color.b = 0.0;
            }
          else
            {
              // make other way-points yellow
              wp.color.r = 1.0;
              wp.color.g = 1.0;
              wp.color.b = 0.0;
            }

          // Add this way-point to the vector of markers to publish
          marks_msg_.markers.push_back(wp);
        }
    }

  pub.publish(marks_msg_);
}

/** Publish global road map */
void MapLanesDriver::publishGlobalMap(void)
{
  art_map::ArtLanes lane_data;
  if (0 == map_->getAllLanes(&lane_data))
    {
      ROS_WARN("no map data available to publish");
      return;
    }

  // the map is in the /map frame of reference with present time
  lane_data.header.stamp = ros::Time::now();
  lane_data.header.frame_id = frame_id_;

  ROS_INFO_STREAM("publishing " <<  lane_data.polygons.size()
                  <<" global roadmap polygons");
  roadmap_global_.publish(lane_data);
#if 0 // only publish local map (for now)
  // publish global map with permanent duration
  publishMapMarks(mapmarks_, "global_roadmap", ros::Duration(), lane_data);
#endif
}

/** Publish current local road map */
void MapLanesDriver::publishLocalMap(void)
{
  art_map::ArtLanes lane_data;
  if (0 != map_->getLanes(&lane_data, MapXY(odom_msg_.pose.pose.position)))
    {
      ROS_DEBUG("no map data available to publish");
      return;
    }

  // the map is in the /map frame of reference with time of the
  // latest odometry message
  lane_data.header.stamp = odom_msg_.header.stamp;
  lane_data.header.frame_id = frame_id_;

  ROS_DEBUG_STREAM("publishing " <<  lane_data.polygons.size()
                   <<" local roadmap polygons");
  roadmap_local_.publish(lane_data);

  // publish local map with temporary duration
  publishMapMarks(mapmarks_, "local_roadmap",
                  ros::Duration(art_common::ArtHertz::MAPLANES), lane_data);
}

/** Spin function for driver thread */
void MapLanesDriver::Spin() 
{
  publishGlobalMap();                   // publish global map once at start

  ros::Rate cycle(art_common::ArtHertz::MAPLANES); // set driver cycle rate

  // Loop publishing MapLanes state until driver Shutdown().
  while(ros::ok())
    {
      ros::spinOnce();                  // handle incoming messages

      if (initial_position_)
        {
          // Publish local roadmap
          publishLocalMap();
        }

      cycle.sleep();                    // sleep until next cycle
    }
}

/** Build road map
 *
 *  @return true if successful
 */  
bool MapLanesDriver::buildRoadMap(void)
{
  if (rndf_name_ == "")
    {
      ROS_FATAL("required ~rndf parameter missing");
      return false;
    }

  RNDF *rndf = new RNDF(rndf_name_);
  
  if (!rndf->is_valid)
    {
      ROS_FATAL("RNDF not valid");
      delete rndf;
      return false;;
    }

  // Allocate a way-point graph.  Populate with nodes from the RNDF,
  // then fill in the MapXY coordinates relative to a UTM grid based
  // on the first way-point in the graph.
  graph_ = new Graph();
  rndf->populate_graph(*graph_);
  graph_->find_mapxy();

  // MapRNDF() saves a pointer to the Graph object, so we can't delete it here.
  // That is why graph_ is a class variable, deleted in the deconstructor.
  // TODO: fix this absurd interface
  int rc = map_->MapRNDF(graph_, poly_size_);
  delete rndf;

  if (rc != 0)
    {
      ROS_FATAL("cannot process RNDF! (%s)", strerror(rc));
      return false;
    }

  return true;
}

/** main program */
int main(int argc, char** argv)
{
  ros::init(argc, argv, "maplanes");
  ros::NodeHandle node;
  MapLanesDriver dvr;

  if (dvr.Setup(node) != 0)
    return 2;
  if (!dvr.buildRoadMap())
    return 3;
  dvr.Spin();
  dvr.Shutdown();

  return 0;
}
