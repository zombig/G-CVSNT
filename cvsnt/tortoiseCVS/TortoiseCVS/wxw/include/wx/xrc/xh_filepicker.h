/////////////////////////////////////////////////////////////////////////////
// Name:        wx/xrc/xh_filepicker.h
// Purpose:     XML resource handler for wxFilePickerCtrl
// Author:      Francesco Montorsi
// Created:     2006-04-17
// RCS-ID:      $Id: xh_filepicker.h,v 1.1 2012/03/04 01:07:54 aliot Exp $
// Copyright:   (c) 2006 Francesco Montorsi
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_XH_FILEPICKERCTRL_H_
#define _WX_XH_FILEPICKERCTRL_H_

#include "wx/xrc/xmlres.h"

#if wxUSE_XRC && wxUSE_FILEPICKERCTRL

class WXDLLIMPEXP_XRC wxFilePickerCtrlXmlHandler : public wxXmlResourceHandler
{
    DECLARE_DYNAMIC_CLASS(wxFilePickerCtrlXmlHandler)

public:
    wxFilePickerCtrlXmlHandler();
    virtual wxObject *DoCreateResource();
    virtual bool CanHandle(wxXmlNode *node);
};

#endif // wxUSE_XRC && wxUSE_FILEPICKERCTRL

#endif // _WX_XH_FILEPICKERCTRL_H_
