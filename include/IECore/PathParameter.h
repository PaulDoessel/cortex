//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2007-2010, Image Engine Design Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of Image Engine Design nor the names of any
//       other contributors to this software may be used to endorse or
//       promote products derived from this software without specific prior
//       written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#ifndef IE_CORE_PATHPARAMETER_H
#define IE_CORE_PATHPARAMETER_H

#include "IECore/Export.h"
#include "IECore/SimpleTypedParameter.h"

namespace IECore
{

/// This base class implements a StringParameter object with validation
/// of the value based on it representing a file/directory path.
class IECORE_API PathParameter : public StringParameter
{
	public :

		IE_CORE_DECLARERUNTIMETYPED( PathParameter, StringParameter )

		typedef enum {
			DontCare,
			MustExist,
			MustNotExist,
		} CheckType;

		PathParameter( const std::string &name, const std::string &description,
			const std::string &defaultValue = "", bool allowEmptyString = true, CheckType check = PathParameter::DontCare,
			const StringParameter::PresetsContainer &presets = StringParameter::PresetsContainer(), bool presetsOnly = false, ConstCompoundObjectPtr userData=0 );

		bool allowEmptyString() const;
		bool mustExist() const;
		bool mustNotExist() const;

		/// Returns false if :
		///
		/// * allowEmptyString() is false and the string is empty.
		/// * The value does not form a valid path name.
		/// * mustExist() is true and the file/dir doesn't exist.
		/// * mustNotExist() is true and the file/dir exists.
		virtual bool valueValid( const Object *value, std::string *reason = 0 ) const;

	private :

		bool m_allowEmptyString;
		CheckType m_check;

};

IE_CORE_DECLAREPTR( PathParameter )

} // namespace IECore

#endif // IE_CORE_PATHPARAMETER_H
