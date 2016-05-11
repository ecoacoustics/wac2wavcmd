# Copyright (C) 2014 Wildlife Acoustics, Inc.
# 
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public LIcense as published by 
# the Free Software Foundation, either version 3 of the License, or 
# (at yuour option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
# Wildlife Acoustics, Inc.
# 3 Clock Tower Place, Suite 210
# Maynard, MA 01754-2549
# +1 978 369 5225
# www.wildlifeacoustics.com
#

wac2wavcmd: wac2wavcmd.c
	gcc -o wac2wavcmd wac2wavcmd.c

.FORCE:
	
