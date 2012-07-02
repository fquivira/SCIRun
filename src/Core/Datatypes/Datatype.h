/*
   For more information, please see: http://software.sci.utah.edu

   The MIT License

   Copyright (c) 2012 Scientific Computing and Imaging Institute,
   University of Utah.

   
   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
*/


#ifndef CORE_DATATYPES_DATATYPE_H
#define CORE_DATATYPES_DATATYPE_H 

#include <boost/shared_ptr.hpp>
#include <boost/optional.hpp>
#include <boost/any.hpp>
#include <Core/Datatypes/Share.h>

namespace SCIRun {
namespace Domain {
namespace Datatypes {

#ifdef BUILD_Core_Datatypes
#pragma message("poot")
#endif

  // hold anything for now
  class SCISHARE Datatype
  {
  public:
    Datatype();
    ~Datatype();
    template <typename T>
    explicit Datatype(const T& t) : value_(t) {}
    Datatype(const Datatype& other);
    Datatype& operator=(const Datatype& rhs);

    template <typename T>
    T getValue()
    {
      return boost::any_cast<T>(value_);
    }

    virtual Datatype* clone() const;

  private:
    boost::any value_;
  };

  typedef boost::shared_ptr<Datatype> DatatypeHandle;
  typedef boost::optional<DatatypeHandle> DatatypeHandleOption;

}}}


#endif
