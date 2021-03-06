/*
	ROM / OS loader, TOS

	Copyright (c) 2005-2006 Patrice Mandin

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef BOOTOSTOS_H
#define BOOTOSTOS_H

#include "aranym_exception.h"
#include "bootos.h"

/* TOS ROM class */

class TosBootOs : public BootOs
{
	public:
		TosBootOs(void) ARANYM_THROWS(AranymException);
		void reset(bool cold) ARANYM_THROWS(AranymException);
		virtual const char *type() { return "TOS"; };
	
	private:
		void tos_patch(bool cold) ARANYM_THROWS(AranymException);
};

#endif /* BOOTOSTOS_H */
