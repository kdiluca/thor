#include <ostream>
#include <iostream>
#include <algorithm>
#include <cmath>

#include "thor/trippathbuilder.h"

#include <valhalla/baldr/datetime.h>
#include <valhalla/baldr/edgeinfo.h>
#include <valhalla/baldr/signinfo.h>
#include <valhalla/baldr/graphconstants.h>
#include <valhalla/midgard/pointll.h>
#include <valhalla/midgard/logging.h>

using namespace valhalla::baldr;
using namespace valhalla::midgard;
using namespace valhalla::odin;

namespace {

// Meters offset from start/end of shape for finding heading
constexpr float kMetersOffsetForHeading = 30.0f;

template<class iter>
void AddPartialShape(std::vector<PointLL>& shape, iter start, iter end,
                     float partial_length, bool back_insert,
                     const PointLL& last) {
  auto push = [&shape, &back_insert] (const PointLL& point) {
    if(back_insert)
    shape.push_back(point);
    else
    shape.insert(shape.begin(), point);
  };

  //yeah we dont add shape if we dont have any length to add
  if (partial_length > 0.f) {
    //if we are adding on to a shape that already has points we dont want to actually add the first one
    if (!back_insert)
      push(*start);
    //for each segment
    for (; start != end - 1; ++start) {
      //is this segment longer than what we have left, then we found the segment the point lies on
      const auto length = (start + 1)->Distance(*start);
      if (length > partial_length) {
        push(last);
        return;
      }
      //just take the point from this segment
      push(*(start + 1));
      partial_length -= length;
    }
  }
}

void TrimShape(std::vector<PointLL>& shape, const float start,
               const PointLL& start_vertex, const float end,
               const PointLL& end_vertex) {
  //clip up to the start point
  float along = 0.f;
  auto current = shape.begin();
  while (current != shape.end() - 1) {
    along += (current + 1)->Distance(*current);
    //just crossed it
    if (along > start) {
      along = start;
      *current = start_vertex;
      shape.erase(shape.begin(), current);
      break;
    }
    ++current;
  }

  //clip after the end point
  current = shape.begin();
  while (current != shape.end() - 1) {
    along += (current + 1)->Distance(*current);
    //just crossed it
    if (along > end) {
      *(++current) = end_vertex;
      shape.erase(++current, shape.end());
      break;
    }
    ++current;
  }
}

uint32_t GetAdminIndex(
    const AdminInfo& admin_info,
    std::unordered_map<AdminInfo, uint32_t, AdminInfo::AdminInfoHasher>& admin_info_map,
    std::vector<AdminInfo>& admin_info_list) {

  uint32_t admin_index = 0;
  auto existing_admin = admin_info_map.find(admin_info);

  // If admin was not processed yet
  if (existing_admin == admin_info_map.end()) {

    // Assign new admin index
    admin_index = admin_info_list.size();

    // Add admin info to list
    admin_info_list.emplace_back(admin_info);

    // Add admin info/index pair to map
    admin_info_map.emplace(admin_info, admin_index);
  }
  // Use known admin
  else {
    admin_index = existing_admin->second;
  }
  return admin_index;
}

void AssignAdmins(TripPath& trip_path,
                  const std::vector<AdminInfo>& admin_info_list) {
  // Assign the admins
  for (const auto& admin_info : admin_info_list) {
    TripPath_Admin* trip_admin = trip_path.add_admin();
    trip_admin->set_country_code(admin_info.country_iso());
    trip_admin->set_country_text(admin_info.country_text());
    trip_admin->set_state_code(admin_info.state_iso());
    trip_admin->set_state_text(admin_info.state_text());
    trip_admin->set_start_dst(admin_info.start_dst());
    trip_admin->set_end_dst(admin_info.end_dst());
  }
}

}

namespace valhalla {
namespace thor {

// Default constructor
TripPathBuilder::TripPathBuilder() {
}

// Destructor
TripPathBuilder::~TripPathBuilder() {
}

// For now just find the length of the path!
// TODO - probably need the location information passed in - to
// add to the TripPath
TripPath TripPathBuilder::Build(GraphReader& graphreader,
                                const std::vector<PathInfo>& path,
                                const PathLocation& origin,
                                const PathLocation& dest) {
  // TripPath is a protocol buffer that contains information about the trip
  TripPath trip_path;

  // TODO - in future we will handle vias
  // Set origin
  TripPath_Location* tp_orig = trip_path.add_location();
  TripPath_LatLng* orig_ll = tp_orig->mutable_ll();
  orig_ll->set_lat(origin.latlng_.lat());
  orig_ll->set_lng(origin.latlng_.lng());
  tp_orig->set_type(
      (origin.stoptype_ == Location::StopType::BREAK) ?
          TripPath_Location_Type_kBreak : TripPath_Location_Type_kThrough);
  if (!origin.name_.empty())
    tp_orig->set_name(origin.name_);
  if (!origin.street_.empty())
    tp_orig->set_street(origin.street_);
  if (!origin.city_.empty())
    tp_orig->set_city(origin.city_);
  if (!origin.state_.empty())
    tp_orig->set_state(origin.state_);
  if (!origin.zip_.empty())
    tp_orig->set_postal_code(origin.zip_);
  if (!origin.country_.empty())
    tp_orig->set_country(origin.country_);
  if (origin.heading_)
    tp_orig->set_heading(*origin.heading_);
  if (origin.date_time_)
    tp_orig->set_date_time(*origin.date_time_);

  // Set destination
  TripPath_Location* tp_dest = trip_path.add_location();
  TripPath_LatLng* dest_ll = tp_dest->mutable_ll();
  dest_ll->set_lat(dest.latlng_.lat());
  dest_ll->set_lng(dest.latlng_.lng());
  tp_dest->set_type(
      (dest.stoptype_ == Location::StopType::BREAK) ?
          TripPath_Location_Type_kBreak : TripPath_Location_Type_kThrough);
  if (!dest.name_.empty())
    tp_dest->set_name(dest.name_);
  if (!dest.street_.empty())
    tp_dest->set_street(dest.street_);
  if (!dest.city_.empty())
    tp_dest->set_city(dest.city_);
  if (!dest.state_.empty())
    tp_dest->set_state(dest.state_);
  if (!dest.zip_.empty())
    tp_dest->set_postal_code(dest.zip_);
  if (!dest.country_.empty())
    tp_dest->set_country(dest.country_);
  if (dest.heading_)
    tp_dest->set_heading(*dest.heading_);
  if (dest.date_time_)
    tp_dest->set_date_time(*dest.date_time_);

  uint32_t origin_sec_from_mid = 0;
  if (origin.date_time_)
    origin_sec_from_mid = DateTime::seconds_from_midnight(*origin.date_time_);

  // Get the first nodes graph id by using the end node of the first edge to get the tile with the opposing edge
  // then use the opposing index to get the opposing edge, and its end node is the begin node of the original edge
  auto* first_edge =
      graphreader.GetGraphTile(path.front().edgeid)->directededge(
          path.front().edgeid);
  auto* first_tile = graphreader.GetGraphTile(first_edge->endnode());
  auto* first_node = first_tile->node(first_edge->endnode());
  GraphId startnode = first_tile->directededge(
      first_node->edge_index() + first_edge->opp_index())->endnode();

  // Partial edge at the start
  auto start_pct = origin.edges().front().dist;
  auto start_vrt = origin.vertex();
  for (size_t i = 1; i < origin.edges().size(); ++i)
    if (origin.edges()[i].id == path.front().edgeid)
      start_pct = origin.edges()[i].dist;

  // Partial edge at the end
  auto end_pct = dest.edges().front().dist;
  auto end_vrt = dest.vertex();
  for (size_t i = 1; i < dest.edges().size(); ++i)
    if (dest.edges()[i].id == path.back().edgeid)
      end_pct = dest.edges()[i].dist;

  // Special case - destination is at a node - end percent is 1
  if (dest.IsNode()) {
    end_pct = 1.0f;
  }

  // Structures to process admins
  std::unordered_map<AdminInfo, uint32_t, AdminInfo::AdminInfoHasher> admin_info_map;
  std::vector<AdminInfo> admin_info_list;
  uint32_t last_node_admin_index;

  // If the path was only one edge we have a special case
  if (path.size() == 1) {
    if (end_pct < start_pct)
      throw std::runtime_error(
          "Generated reverse trivial path, report this bug!");
    const auto tile = graphreader.GetGraphTile(path.front().edgeid);
    const auto edge = tile->directededge(path.front().edgeid);

    // Sort out the shape
    auto shape = tile->edgeinfo(edge->edgeinfo_offset())->shape();
    if (!edge->forward())
      std::reverse(shape.begin(), shape.end());
    float total = static_cast<float>(edge->length());
    TrimShape(shape, start_pct * total, start_vrt, end_pct * total, end_vrt);

    auto trip_edge = AddTripEdge(path.front().edgeid.id(), path.front().trip_id,
                                 edge, trip_path.add_node(), tile,
                                 end_pct - start_pct);
    trip_edge->set_begin_shape_index(0);
    trip_edge->set_end_shape_index(shape.size());
    auto* node = trip_path.add_node();
    node->set_elapsed_time(path.front().elapsed_time);
    node->set_admin_index(
        GetAdminIndex(
            tile->admininfo(tile->node(edge->endnode())->admin_index()),
            admin_info_map, admin_info_list));
    trip_path.set_shape(encode<std::vector<PointLL> >(shape));
    // Assign the trip path admins
    AssignAdmins(trip_path, admin_info_list);
    return trip_path;
  }

  // Iterate through path
  float elapsedtime = 0.0f;
  uint32_t prior_opp_local_index = -1;
  std::vector<PointLL> trip_shape;
  for (auto edge_itr = path.begin(); edge_itr != path.end(); ++edge_itr) {
    const GraphId& edge = edge_itr->edgeid;
    const uint32_t trip_id = edge_itr->trip_id;
    const GraphTile* graphtile = graphreader.GetGraphTile(edge);
    const DirectedEdge* directededge = graphtile->directededge(edge);

    // Skip transition edges
    if (directededge->trans_up() || directededge->trans_down()) {
      // TODO - remove debug stuff later.
      if (directededge->trans_up()) {
        LOG_TRACE("Transition up!");
      } else {
        LOG_TRACE("Transition down!");
      }
      // Get the end node - set as the startnode (for connected edges at
      // next iteration).
      startnode = directededge->endnode();

      continue;
    }

    // Add a node to the trip path and set its attributes.
    TripPath_Node* trip_node = trip_path.add_node();

    // Set node attributes - only set if they are true since they are optional
    if (graphtile->node(startnode)->type() == NodeType::kStreetIntersection)
      trip_node->set_street_intersection(true);

    if (graphtile->node(startnode)->type() == NodeType::kGate)
      trip_node->set_gate(true);

    if (graphtile->node(startnode)->type() == NodeType::kBollard)
      trip_node->set_bollard(true);

    if (graphtile->node(startnode)->type() == NodeType::kTollBooth)
      trip_node->set_toll_booth(true);

    if (graphtile->node(startnode)->type() == NodeType::kBikeShare)
      trip_node->set_bike_share(true);

    if (graphtile->node(startnode)->type() == NodeType::kParking)
      trip_node->set_parking(true);

    // Assign the elapsed time from the start of the leg
    trip_node->set_elapsed_time(elapsedtime);

    if (graphtile->node(startnode)->is_transit()) {
      trip_node->set_transit_stop(true);
      trip_node->set_transit_parent_stop(graphtile->node(startnode)->parent());

      const TransitStop* stop = graphtile->GetTransitStop(
          graphtile->node(startnode)->stop_id());

      TripPath_TransitStopInfo* transit_stop_info = trip_node
          ->mutable_transit_stop_info();
      transit_stop_info->set_name(graphtile->GetName(stop->name_offset()));
      if (trip_id) {
        const TransitDeparture* transit_departure = graphtile
            ->GetTransitDeparture(graphtile->directededge(edge.id())->lineid(), trip_id);

        transit_stop_info->set_departure_date_time(
            DateTime::get_duration(*origin.date_time_,
                                   transit_departure->departure_time() -
                                   origin_sec_from_mid));
        transit_stop_info->set_arrival_date_time(
            DateTime::get_duration(*origin.date_time_,
                                   (transit_departure->departure_time() +
                                       transit_departure->elapsed_time()) -
                                       origin_sec_from_mid));
      }
    }

    // Assign the admin index
    trip_node->set_admin_index(
        GetAdminIndex(
            graphtile->admininfo(graphtile->node(startnode)->admin_index()),
            admin_info_map, admin_info_list));

    // Add edge to the trip node and set its attributes
    auto is_first_edge = edge_itr == path.begin();
    auto is_last_edge = edge_itr == path.end() - 1;
    float length_pct = (
        is_first_edge ? 1.f - start_pct : (is_last_edge ? end_pct : 1.f));
    TripPath_Edge* trip_edge = AddTripEdge(edge.id(), trip_id, directededge,
                                           trip_node, graphtile, length_pct);

    // Get the shape and set shape indexes (directed edge forward flag
    // determines whether shape is traversed forward or reverse).
    std::unique_ptr<const EdgeInfo> edgeinfo = graphtile->edgeinfo(
        directededge->edgeinfo_offset());
    if (is_first_edge) {
      trip_edge->set_begin_shape_index(0);
    } else {
      trip_edge->set_begin_shape_index(trip_shape.size() - 1);
    }

    // We need to clip the shape if its at the beginning or end and isnt a full length
    if (is_first_edge || is_last_edge) {
      float length = static_cast<float>(directededge->length()) * length_pct;
      if (directededge->forward() == is_last_edge) {
        AddPartialShape<std::vector<PointLL>::const_iterator>(
            trip_shape, edgeinfo->shape().begin(), edgeinfo->shape().end(),
            length, is_last_edge, is_last_edge ? end_vrt : start_vrt);
      } else {
        AddPartialShape<std::vector<PointLL>::const_reverse_iterator>(
            trip_shape, edgeinfo->shape().rbegin(), edgeinfo->shape().rend(),
            length, is_last_edge, is_last_edge ? end_vrt : start_vrt);
      }
    }    // Just get the shape in there in the right direction
    else {
      if (directededge->forward())
        trip_shape.insert(trip_shape.end(), edgeinfo->shape().begin() + 1,
                          edgeinfo->shape().end());
      else
        trip_shape.insert(trip_shape.end(), edgeinfo->shape().rbegin() + 1,
                          edgeinfo->shape().rend());
    }
    trip_edge->set_end_shape_index(trip_shape.size() - 1);

    // Add connected edges from the start node. Do this after the first trip
    // edge is added
    //
    //Our path is from 1 to 2 to 3 (nodes) to ... n nodes.
    //Each letter represents the edge info.
    //So at node 2, we will store the edge info for D and we will store the
    //intersecting edge info for B, C, E, F, and G.  We need to make sure
    //that we don't store the edge info from A and D again.  Also, do not store transition edges.
    //
    //     (X)    (3)   (X)
    //       \\   ||   //
    //      C \\ D|| E//
    //         \\ || //
    //      B   \\||//   F
    // (X)======= (2) ======(X)
    //            ||\\
    //          A || \\ G
    //            ||  \\
    //            (1)  (X)
    if (startnode.Is_Valid()) {
      // Get the graph tile and the first edge from the node
      const GraphTile* tile = graphreader.GetGraphTile(startnode);
      const NodeInfo* nodeinfo = tile->node(startnode);
      uint32_t edgeid = nodeinfo->edge_index();

      for (uint32_t edge_idx = 0; edge_idx < nodeinfo->local_edge_count();
          ++edge_idx) {
        // If the edge index is the previous local edge or the current local edge
        // then skip it
        if ((edge_idx == prior_opp_local_index)
            || (edge_idx == directededge->localedgeidx())) {
          continue;
        }
        AddTripIntersectingEdge(edge_idx, prior_opp_local_index,
                                directededge->localedgeidx(), nodeinfo,
                                trip_node);
      }
    }

    // Update elapsed time at the end of the edge, store this at the next node.
    elapsedtime = edge_itr->elapsed_time;

    // Set the endnode of this directed edge as the startnode of the next edge.
    startnode = directededge->endnode();

    // Save the index of the opposing local directed edge at the end node
    prior_opp_local_index = directededge->opp_local_idx();
  }

  // Add the last node
  auto* node = trip_path.add_node();
  auto* last_tile = graphreader.GetGraphTile(startnode);
  node->set_admin_index(
      GetAdminIndex(
          last_tile->admininfo(last_tile->node(startnode)->admin_index()),
          admin_info_map, admin_info_list));
  node->set_elapsed_time(elapsedtime);

  // Assign the admins
  AssignAdmins(trip_path, admin_info_list);

  // Encode shape and add to trip path.
  std::string encoded_shape_ = encode<std::vector<PointLL> >(trip_shape);
  trip_path.set_shape(encoded_shape_);

  //hand it back
  return trip_path;
}

namespace {
TripPath_RoadClass GetTripPathRoadClass(RoadClass road_class) {
  switch (road_class) {
    case RoadClass::kMotorway:
      return TripPath_RoadClass_kMotorway;
    case RoadClass::kTrunk:
      return TripPath_RoadClass_kTrunk;
    case RoadClass::kPrimary:
      return TripPath_RoadClass_kPrimary;
    case RoadClass::kSecondary:
      return TripPath_RoadClass_kSecondary;
    case RoadClass::kTertiary:
      return TripPath_RoadClass_kTertiary;
    case RoadClass::kUnclassified:
      return TripPath_RoadClass_kUnclassified;
    case RoadClass::kResidential:
      return TripPath_RoadClass_kResidential;
    case RoadClass::kServiceOther:
      return TripPath_RoadClass_kServiceOther;
  }
}

TripPath_Driveability GetTripPathDriveability(Driveability driveability) {
  switch (driveability) {
    case Driveability::kNone:
      return TripPath_Driveability_kNone;
    case Driveability::kForward:
      return TripPath_Driveability_kForward;
    case Driveability::kBackward:
      return TripPath_Driveability_kBackward;
    case Driveability::kBoth:
      return TripPath_Driveability_kBoth;
  }
}

}

// Add a trip edge to the trip node and set its attributes
TripPath_Edge* TripPathBuilder::AddTripEdge(const uint32_t idx,
                                            const uint32_t trip_id,
                                            const DirectedEdge* directededge,
                                            TripPath_Node* trip_node,
                                            const GraphTile* graphtile,
                                            const float length_percentage) {

  TripPath_Edge* trip_edge = trip_node->mutable_edge();

  // Get the edgeinfo and list of names - add to the trip edge.
  std::unique_ptr<const EdgeInfo> edgeinfo = graphtile->edgeinfo(
      directededge->edgeinfo_offset());
  std::vector<std::string> names = edgeinfo->GetNames();
  for (const auto& name : names) {
    trip_edge->add_name(name);
  }

#ifdef LOGGING_LEVEL_TRACE
  LOG_TRACE(std::string("wayid=") + std::to_string(edgeinfo->wayid()));
#endif

  // Set the exits (if the directed edge has exit sign information)
  if (directededge->exitsign()) {
    std::vector<SignInfo> signs = graphtile->GetSigns(idx);
    if (!signs.empty()) {
      TripPath_Sign* trip_exit = trip_edge->mutable_sign();
      for (const auto& sign : signs) {
        switch (sign.type()) {
          case Sign::Type::kExitNumber: {
            trip_exit->add_exit_number(sign.text());
            break;
          }
          case Sign::Type::kExitBranch: {
            trip_exit->add_exit_branch(sign.text());
            break;
          }
          case Sign::Type::kExitToward: {
            trip_exit->add_exit_toward(sign.text());
            break;
          }
          case Sign::Type::kExitName: {
            trip_exit->add_exit_name(sign.text());
            break;
          }
        }
      }
    }
  }

  // Set road class
  trip_edge->set_road_class(
      GetTripPathRoadClass(directededge->classification()));

  // Set speed and length
  trip_edge->set_length(directededge->length() * 0.001f * length_percentage);  // Convert to km
  trip_edge->set_speed(directededge->speed());

  // Test whether edge is traversed forward or reverse and set driveability and heading
  if (directededge->forward()) {
    if ((directededge->forwardaccess() & kAutoAccess)
        && (directededge->reverseaccess() & kAutoAccess))
      trip_edge->set_driveability(
          TripPath_Driveability::TripPath_Driveability_kBoth);
    else if ((directededge->forwardaccess() & kAutoAccess)
        && !(directededge->reverseaccess() & kAutoAccess))
      trip_edge->set_driveability(
          TripPath_Driveability::TripPath_Driveability_kForward);
    else if (!(directededge->forwardaccess() & kAutoAccess)
        && (directededge->reverseaccess() & kAutoAccess))
      trip_edge->set_driveability(
          TripPath_Driveability::TripPath_Driveability_kBackward);
    else
      trip_edge->set_driveability(
          TripPath_Driveability::TripPath_Driveability_kNone);

    trip_edge->set_begin_heading(
        std::round(
            PointLL::HeadingAlongPolyline(edgeinfo->shape(),
                                          kMetersOffsetForHeading)));
    trip_edge->set_end_heading(
        std::round(
            PointLL::HeadingAtEndOfPolyline(edgeinfo->shape(),
                                            kMetersOffsetForHeading)));
  } else {
    // Reverse driveability and heading
    if ((directededge->forwardaccess() & kAutoAccess)
        && (directededge->reverseaccess() & kAutoAccess))
      trip_edge->set_driveability(
          TripPath_Driveability::TripPath_Driveability_kBoth);
    else if (!(directededge->forwardaccess() & kAutoAccess)
        && (directededge->reverseaccess() & kAutoAccess))
      trip_edge->set_driveability(
          TripPath_Driveability::TripPath_Driveability_kForward);
    else if ((directededge->forwardaccess() & kAutoAccess)
        && !(directededge->reverseaccess() & kAutoAccess))
      trip_edge->set_driveability(
          TripPath_Driveability::TripPath_Driveability_kBackward);
    else
      trip_edge->set_driveability(
          TripPath_Driveability::TripPath_Driveability_kNone);

    trip_edge->set_begin_heading(
        std::round(
            fmod(
                (PointLL::HeadingAtEndOfPolyline(edgeinfo->shape(),
                                                 kMetersOffsetForHeading)
                    + 180.0f),
                360)));

    trip_edge->set_end_heading(
        std::round(
            fmod(
                (PointLL::HeadingAlongPolyline(edgeinfo->shape(),
                                               kMetersOffsetForHeading) + 180.0f),
                360)));
  }

  // Set ramp / turn channel flag
  if (directededge->link()) {
    if (directededge->use() == Use::kRamp)
      trip_edge->set_ramp(true);
    else if (directededge->use() == Use::kTurnChannel)
      trip_edge->set_turn_channel(true);
  }

  // Set all of the use cases - only set if they are true since they are optional
  if (directededge->use() == Use::kRoad)
    trip_edge->set_road(true);

  if (directededge->use() == Use::kTrack)
    trip_edge->set_track(true);

  if (directededge->use() == Use::kDriveway)
    trip_edge->set_driveway(true);

  if (directededge->use() == Use::kAlley)
    trip_edge->set_alley(true);

  if (directededge->use() == Use::kParkingAisle)
    trip_edge->set_parking_aisle(true);

  if (directededge->use() == Use::kEmergencyAccess)
    trip_edge->set_emergency_access(true);

  if (directededge->use() == Use::kDriveThru)
    trip_edge->set_drive_thru(true);

  if (directededge->use() == Use::kCuldesac)
    trip_edge->set_culdesac(true);

  if (directededge->use() == Use::kFootway)
    trip_edge->set_footway(true);

  if (directededge->use() == Use::kSteps)
    trip_edge->set_stairs(true);

  if (directededge->use() == Use::kCycleway)
    trip_edge->set_cycleway(true);

  if (directededge->use() == Use::kMountainBike)
    trip_edge->set_mountain_bike(true);

  if (directededge->use() == Use::kRail)
    trip_edge->set_rail(true);

  if (directededge->use() == Use::kBus)
    trip_edge->set_bus(true);

  if (directededge->use() == Use::kTransitConnection)
    trip_edge->set_transit_connection(true);

  if (directededge->use() == Use::kOther)
    trip_edge->set_other(true);

  // Set edge attributes - only set if they are true since they are optional
  if (directededge->ferry())
    trip_edge->set_ferry(true);

  if (directededge->railferry())
    trip_edge->set_rail_ferry(true);

  if (directededge->toll())
    trip_edge->set_toll(true);

  if (directededge->unpaved())
    trip_edge->set_unpaved(true);

  if (directededge->tunnel())
    trip_edge->set_tunnel(true);

  if (directededge->bridge())
    trip_edge->set_bridge(true);

  if (directededge->roundabout())
    trip_edge->set_roundabout(true);

  if (directededge->internal())
    trip_edge->set_internal_intersection(true);

  if (directededge->drive_on_right())
    trip_edge->set_drive_on_right(true);

  if (trip_id
      && (directededge->use() == Use::kRail || directededge->use() == Use::kBus)) {

    if (directededge->use() == Use::kRail)
      trip_edge->set_transit_type(
          TripPath_TransitType::TripPath_TransitType_kTrain);

    if (directededge->use() == Use::kBus)
      trip_edge->set_transit_type(
          TripPath_TransitType::TripPath_TransitType_kBus);

    TripPath_TransitInfo* transit_info = trip_edge->mutable_transit_info();
    const TransitDeparture* transit_departure = graphtile->GetTransitDeparture(
        directededge->lineid(), trip_id);
    const TransitRoute* transit_route = graphtile->GetTransitRoute(
        transit_departure->routeid());
    const TransitTrip* transit_trip = graphtile->GetTransitTrip(trip_id);

    //use route short name if available otherwise trip short name.
    if (transit_route->short_name_offset())
      transit_info->set_short_name(
          graphtile->GetName(transit_route->short_name_offset()));
    else if (transit_trip->short_name_offset())
      transit_info->set_short_name(
          graphtile->GetName(transit_trip->short_name_offset()));

    if (transit_route->long_name_offset())
      transit_info->set_long_name(
          graphtile->GetName(transit_route->long_name_offset()));

    if (transit_departure->headsign_offset())
      transit_info->set_headsign(
          graphtile->GetName(transit_departure->headsign_offset()));
  }

  return trip_edge;
}

void TripPathBuilder::AddTripIntersectingEdge(uint32_t edge_index,
                                              uint32_t prev_edge_index,
                                              uint32_t curr_edge_index,
                                              const baldr::NodeInfo* nodeinfo,
                                              odin::TripPath_Node* trip_node) {

  TripPath_IntersectingEdge* itersecting_edge =
      trip_node->add_intersecting_edge();

  // Set the heading for the intersecting edge
  itersecting_edge->set_begin_heading(nodeinfo->heading(edge_index));

  // Set the driveability flag for the intersecting edge
  itersecting_edge->set_driveability(
      GetTripPathDriveability(nodeinfo->local_driveability(edge_index)));

  // Set the previous/intersecting edge name consistency
  itersecting_edge->set_prev_name_consistency(
      nodeinfo->name_consistency(prev_edge_index, edge_index));

  // Set the current/intersecting edge name consistency
  itersecting_edge->set_curr_name_consistency(
      nodeinfo->name_consistency(curr_edge_index, edge_index));
}

}
}
