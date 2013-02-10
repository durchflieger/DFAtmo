# ---
# Copyright (C) 2011,2012 Andreas Auras <yak54@inkennet.de>
#
# This file is part of DFAtmo the driver for 'Atmolight' controllers for XBMC and xinelib based video players.
#
# DFAtmo is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# DFAtmo is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
#
# This is the DFAtmo XBMC service addon.
#
# ---

if ( __name__ == "__main__" ):
  import xbmc, xbmcaddon
  addon = xbmcaddon.Addon(id='script.dfatmo')
  if addon.getSetting('enabled') == 'true':
    xbmc.executebuiltin('RunAddon("script.dfatmo")')
