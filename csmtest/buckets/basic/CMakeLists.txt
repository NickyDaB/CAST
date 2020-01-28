#================================================================================
#
#    csmtest/buckets/basic/CMakeLists.txt
#
#  © Copyright IBM Corporation 2015-2020. All Rights Reserved
#
#    This program is licensed under the terms of the Eclipse Public License
#    v1.0 as published by the Eclipse Foundation and available at
#    http://www.eclipse.org/legal/epl-v10.html
#
#    U.S. Government Users Restricted Rights:  Use, duplication or disclosure
#    restricted by GSA ADP Schedule Contract with IBM Corp.
#
#================================================================================

# Files that should be executable when shipped

file(GLOB INSTALL_PROGRAMS
  "allocation.sh"
  "bb.sh"
  "compute_node.sh"
  "csm_ctrl_cmd.sh"
  "db_connections_script.sh"
  "db_ras_type_script.sh"
  "db_script.sh"
  "db_stats_script.sh"
  "fvt_allocation_create_and_delete.py"
  "fvt_node_attributes_query_and_update.py"
  "hcdiag.sh"
  "ib_inventory.sh"
  "inventory_collection.sh"
  "jsrun_cmd.sh"
  "node.sh"
  "node_inventory_db_history.sh"
  "pamd.sh"
  "python_libraries.sh"
  "ras.sh"
  "soft_failure_recovery.sh"
  "step.sh"
  "switch_inventory.sh"
)

install(PROGRAMS ${INSTALL_PROGRAMS} COMPONENT csm-test DESTINATION csm/test/buckets/basic)

# CPACK_RPM settings for the csm-test rpm are managed in csmd/setupRPM.cmake 