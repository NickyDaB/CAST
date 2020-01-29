#================================================================================
#
#    csmtest/setup/CMakeLists.txt
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

# Set up and install the directory structure

# executable files
file(GLOB INSTALL_PROGRAMS
    "csm_dual_aggregator_setup.sh"
    "csm_install.sh"
    "csm_setup.sh"
    "csm_uninstall.sh"
)

install(PROGRAMS ${INSTALL_PROGRAMS} COMPONENT csm-test DESTINATION csm/test/setup)