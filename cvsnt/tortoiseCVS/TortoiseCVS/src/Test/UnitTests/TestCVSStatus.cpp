// TortoiseCVS - a Windows shell extension for easy version control

// Copyright (C) 2005 - Torsten Martinsen
// <torsten@tiscali.dk> - November 2005

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
#include <cppunit/extensions/HelperMacros.h>
#include "suite.h"

#include <CVSGlue/CVSStatus.h>

class TestCVSStatus : public CppUnit::TestFixture
{
public:
   void TestGetFileStatus()
   {
      /*
        StatusTest("c:\\autoexec.bat");
        StatusTest("c:\\user\\tma\\tcvs\\head\\TortoiseCVS\\src\\TortoiseCVS.opt");
        StatusTest("c:\\temp\\New Text Document.txt");
      */
   }

   void TestRecursiveNeedsAdding()
   {
   }

   void TestRecursiveScan()
   {
   }

   CPPUNIT_TEST_SUITE(TestCVSStatus);
   CPPUNIT_TEST(TestGetFileStatus);
   CPPUNIT_TEST(TestRecursiveNeedsAdding);
   CPPUNIT_TEST(TestRecursiveScan);
   CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(TestCVSStatus, Suites::CVSGlue());