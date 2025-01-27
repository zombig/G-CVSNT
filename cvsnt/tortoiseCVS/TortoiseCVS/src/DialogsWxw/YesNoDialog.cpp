// TortoiseCVS - a Windows shell extension for easy version control

// Copyright (C) 2000 - Francis Irving
// <francis@flourish.org> - September 2000

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.


#include "StdAfx.h"

#include <wx/wx.h>
#include "YesNoDialog.h"
#include "../Utils/Translate.h"
#include "WxwHelpers.h"

bool DoYesNoDialog(wxWindow* parent, const wxString& message, bool yesdefault)
{
   wxMessageDialog dlg(parent, message.c_str(), _("TortoiseCVS Question"),
                       wxYES_NO |
                       (yesdefault ? wxYES_DEFAULT : wxNO_DEFAULT));
   int ret = dlg.ShowModal();
   return ret == wxID_YES;
}

