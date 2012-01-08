//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2011, Image Engine Design Inc. All rights reserved.
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

#include "OpenEXR/ImathBoxAlgo.h"

#include "boost/format.hpp"

#include "IECore/MessageHandler.h"
#include "IECore/MeshPrimitive.h"
#include "IECore/Camera.h"
#include "IECore/Transform.h"
#include "IECore/SimpleTypedData.h"
#include "IECore/CurvesPrimitive.h"

#include "IECoreArnold/private/RendererImplementation.h"
#include "IECoreArnold/ToArnoldMeshConverter.h"
#include "IECoreArnold/ToArnoldCameraConverter.h"
#include "IECoreArnold/ToArnoldCurvesConverter.h"

using namespace IECore;
using namespace IECoreArnold;
using namespace Imath;
using namespace std;
using namespace boost;

////////////////////////////////////////////////////////////////////////
// AttributeState implementation
////////////////////////////////////////////////////////////////////////

RendererImplementation::AttributeState::AttributeState()
{
	surfaceShader = AiNode( "utility" );
	attributes = new CompoundData;
}

RendererImplementation::AttributeState::AttributeState( const AttributeState &other )
{
	surfaceShader = other.surfaceShader;
	attributes = other.attributes->copy();
}
				
////////////////////////////////////////////////////////////////////////
// RendererImplementation implementation
////////////////////////////////////////////////////////////////////////

extern AtNodeMethods *ieDisplayDriverMethods;

IECoreArnold::RendererImplementation::RendererImplementation()
	:	m_defaultFilter( 0 )
{
	constructCommon( Render );
}

IECoreArnold::RendererImplementation::RendererImplementation( const std::string &assFileName )
	:	m_defaultFilter( 0 )
{
	m_assFileName = assFileName;
	constructCommon( AssGen );
}

IECoreArnold::RendererImplementation::RendererImplementation( const RendererImplementation &other )
{
	constructCommon( Procedural );
	m_transformStack.push( other.m_transformStack.top() );
	m_attributeStack.push( AttributeState( other.m_attributeStack.top() ) );
}

IECoreArnold::RendererImplementation::RendererImplementation( const AtNode *proceduralNode )
{
	constructCommon( Procedural );
	/// \todo Initialise stacks properly!!
	m_transformStack.push( M44f() );
	m_attributeStack.push( AttributeState() );
}

void IECoreArnold::RendererImplementation::constructCommon( Mode mode )
{
	m_mode = mode;
	if( mode != Procedural )
	{
		AiBegin();
	
		/// \todo Control with an option
		AiMsgSetConsoleFlags( AI_LOG_ALL );

		const char *pluginPaths = getenv( "ARNOLD_PLUGIN_PATH" );
		if( pluginPaths )
		{
			AiLoadPlugins( pluginPaths );
		}
		
		// create a generic filter we can use for all displays
		m_defaultFilter = AiNode( "gaussian_filter" );
		AiNodeSetStr( m_defaultFilter, "name", "ieCoreArnold:defaultFilter" );

		m_transformStack.push( M44f() );
		m_attributeStack.push( AttributeState() );
	}
}

IECoreArnold::RendererImplementation::~RendererImplementation()
{
	if( m_mode != Procedural )
	{
		AiEnd();
	}
}

////////////////////////////////////////////////////////////////////////
// options
////////////////////////////////////////////////////////////////////////

void IECoreArnold::RendererImplementation::setOption( const std::string &name, IECore::ConstDataPtr value )
{
	if( 0 == name.compare( 0, 3, "ai:" ) )
	{
		AtNode *options = AiUniverseGetOptions();
		const AtParamEntry *parameter = AiNodeEntryLookUpParameter( AiNodeGetNodeEntry( options ), name.c_str() + 3 );
		if( parameter )
		{
			ToArnoldConverter::setParameter( options, name.c_str() + 3, value );
			return;
		}
	}
	else if( 0 == name.compare( 0, 5, "user:" ) )
	{
		AtNode *options = AiUniverseGetOptions();
		ToArnoldConverter::setParameter( options, name.c_str(), value );
		return;
	}
	else if( name.find_first_of( ":" )!=string::npos )
	{
		// ignore options prefixed for some other renderer
		return;
	}
	
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::setOption", format( "Unknown option \"%s\"." ) % name );	
}

IECore::ConstDataPtr IECoreArnold::RendererImplementation::getOption( const std::string &name ) const
{
	if( 0 == name.compare( 0, 3, "ai:" ) )
	{
		AtNode *options = AiUniverseGetOptions();
		return ToArnoldConverter::getParameter( options, name.c_str() + 3 );
	}
	else if( 0 == name.compare( 0, 5, "user:" ) )
	{
		AtNode *options = AiUniverseGetOptions();
		return ToArnoldConverter::getParameter( options, name.c_str() );
	}
	else if( name == "shutter" )
	{
		AtNode *camera = AiUniverseGetCamera();
		float start = AiNodeGetFlt( camera, "shutter_start" );
		float end = AiNodeGetFlt( camera, "shutter_end" );
		return new V2fData( V2f( start, end ) );
	}

	return 0;
}

void IECoreArnold::RendererImplementation::camera( const std::string &name, const IECore::CompoundDataMap &parameters )
{
	CameraPtr cortexCamera = new Camera( name, 0, new CompoundData( parameters ) );
	ToArnoldCameraConverterPtr converter = new ToArnoldCameraConverter( cortexCamera );
	AtNode *arnoldCamera = converter->convert();
	AtNode *options = AiUniverseGetOptions();
	AiNodeSetPtr( options, "camera", arnoldCamera );
	
	applyTransformToNode( arnoldCamera );
	
	ConstV2iDataPtr resolution = cortexCamera->parametersData()->member<V2iData>( "resolution" );
	AiNodeSetInt( options, "xres", resolution ? resolution->readable().x : 640 );
	AiNodeSetInt( options, "yres", resolution ? resolution->readable().y : 480 );
}

void IECoreArnold::RendererImplementation::display( const std::string &name, const std::string &type, const std::string &data, const IECore::CompoundDataMap &parameters )
{
	AtNode *driver = AiNode( type.c_str() );
	if( !driver )
	{
		msg( Msg::Error, "IECoreArnold::RendererImplementation::display", boost::format( "Unable to create display of type \"%s\"" ) % type );
		return;
	}
		
	string nodeName = boost::str( boost::format( "ieCoreArnold:display%d" ) % m_outputDescriptions.size() );
	AiNodeSetStr( driver, "name", nodeName.c_str() );
	
	const AtParamEntry *fileNameParameter = AiNodeEntryLookUpParameter( AiNodeGetNodeEntry( driver ), "filename" );
	if( fileNameParameter )
	{
		AiNodeSetStr( driver, AiParamGetName( fileNameParameter ), name.c_str() );
	}

	ToArnoldConverter::setParameters( driver, parameters );
	
	string d = data;
	if( d=="rgb" )
	{
		d = "RGB RGB";
	}
	else if( d=="rgba" )
	{
		d = "RGBA RGBA";
	}
	
	std::string outputDescription = str( format( "%s %s %s" ) % d.c_str() % AiNodeGetName( m_defaultFilter ) % nodeName.c_str() );
	m_outputDescriptions.push_back( outputDescription );
}

/////////////////////////////////////////////////////////////////////////////////////////
// world
/////////////////////////////////////////////////////////////////////////////////////////

void IECoreArnold::RendererImplementation::worldBegin()
{
	// reset transform stack
	if( m_transformStack.size() > 1 )
	{
		msg( Msg::Warning, "IECoreArnold::RendererImplementation::worldBegin", "Missing transformEnd() call detected." );
		while( m_transformStack.size() > 1 )
		{
			m_transformStack.pop();
		}
		m_transformStack.top() = M44f();
	}
	
	// specify default camera if none has been specified yet
	AtNode *options = AiUniverseGetOptions();

	if( !AiNodeGetPtr( options, "camera" ) )
	{
		// no camera has been specified - make a default one
		camera( "ieCoreArnold:defaultCamera", CompoundDataMap() );
	}

	// specify all the outputs
	AtArray *outputsArray = AiArrayAllocate( m_outputDescriptions.size(), 1, AI_TYPE_STRING );
	for( int i = 0, e = m_outputDescriptions.size(); i < e; i++ )
	{
		AiArraySetStr( outputsArray, 0, m_outputDescriptions[i].c_str() );
	}
	AiNodeSetArray( options, "outputs", outputsArray ); 

}

void IECoreArnold::RendererImplementation::worldEnd()
{
	if( m_mode == Render )
	{
		AiRender( AI_RENDER_MODE_CAMERA );
	}
	else if( m_mode == AssGen )
	{
		AiASSWrite( m_assFileName.c_str(), AI_NODE_ALL, true );
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// transforms
/////////////////////////////////////////////////////////////////////////////////////////

void IECoreArnold::RendererImplementation::transformBegin()
{
	m_transformStack.push( ( m_transformStack.top() ) );	
}

void IECoreArnold::RendererImplementation::transformEnd()
{
	if( m_transformStack.size() <= 1 )
	{
		msg( Msg::Warning, "IECoreArnold::RendererImplementation::transformEnd", "No matching transformBegin() call." );
		return;
	}

	m_transformStack.pop();
}

void IECoreArnold::RendererImplementation::setTransform( const Imath::M44f &m )
{
	m_transformStack.top() = m;
}

void IECoreArnold::RendererImplementation::setTransform( const std::string &coordinateSystem )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::setTransform", "Not implemented" );
}

Imath::M44f IECoreArnold::RendererImplementation::getTransform() const
{
	return m_transformStack.top();
}

Imath::M44f IECoreArnold::RendererImplementation::getTransform( const std::string &coordinateSystem ) const
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::getTransform", "Not implemented" );
	M44f result;
	return result;
}

void IECoreArnold::RendererImplementation::concatTransform( const Imath::M44f &m )
{
	m_transformStack.top() = m * m_transformStack.top();
}

void IECoreArnold::RendererImplementation::coordinateSystem( const std::string &name )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::coordinateSystem", "Not implemented" );
}

//////////////////////////////////////////////////////////////////////////////////////////
// attribute code
//////////////////////////////////////////////////////////////////////////////////////////

void IECoreArnold::RendererImplementation::attributeBegin()
{
	transformBegin();
	m_attributeStack.push( AttributeState( m_attributeStack.top() ) );
}

void IECoreArnold::RendererImplementation::attributeEnd()
{
	m_attributeStack.pop();
	transformEnd();
}

void IECoreArnold::RendererImplementation::setAttribute( const std::string &name, IECore::ConstDataPtr value )
{
	m_attributeStack.top().attributes->writable()[name] = value->copy();
}

IECore::ConstDataPtr IECoreArnold::RendererImplementation::getAttribute( const std::string &name ) const
{
	return m_attributeStack.top().attributes->member<Data>( name );
}

void IECoreArnold::RendererImplementation::shader( const std::string &type, const std::string &name, const IECore::CompoundDataMap &parameters )
{
	if( type=="surface" )
	{
		AtNode *s = AiNode( name.c_str() );
		if( !s )
		{
			msg( Msg::Warning, "IECoreArnold::RendererImplementation::shader", boost::format( "Couldn't load shader \"%s\"" ) % name );
			return;
		}
		
		ToArnoldConverter::setParameters( s, parameters );
		
		m_attributeStack.top().surfaceShader = s;
	}
	else
	{
		msg( Msg::Warning, "IECoreArnold::RendererImplementation::shader", boost::format( "Unsupported shader type \"%s\"" ) % type );
	}
}

void IECoreArnold::RendererImplementation::light( const std::string &name, const std::string &handle, const IECore::CompoundDataMap &parameters )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::light", "Not implemented" );
}

void IECoreArnold::RendererImplementation::illuminate( const std::string &lightHandle, bool on )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::illuminate", "Not implemented" );
}

/////////////////////////////////////////////////////////////////////////////////////////
// motion blur
/////////////////////////////////////////////////////////////////////////////////////////

void IECoreArnold::RendererImplementation::motionBegin( const std::set<float> &times )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::motionBegin", "Not implemented" );
}

void IECoreArnold::RendererImplementation::motionEnd()
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::motionEnd", "Not implemented" );
}

/////////////////////////////////////////////////////////////////////////////////////////
// primitives
/////////////////////////////////////////////////////////////////////////////////////////

void IECoreArnold::RendererImplementation::points( size_t numPoints, const IECore::PrimitiveVariableMap &primVars )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::points", "Not implemented" );
}

void IECoreArnold::RendererImplementation::disk( float radius, float z, float thetaMax, const IECore::PrimitiveVariableMap &primVars )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::disk", "Not implemented" );
}

void IECoreArnold::RendererImplementation::curves( const IECore::CubicBasisf &basis, bool periodic, ConstIntVectorDataPtr numVertices, const IECore::PrimitiveVariableMap &primVars )
{
	CurvesPrimitivePtr curves = new IECore::CurvesPrimitive( numVertices, basis, periodic );
	curves->variables = primVars;
	
	ToArnoldCurvesConverterPtr converter = new ToArnoldCurvesConverter( curves );
	AtNode *shape = converter->convert();

	addShape( shape );
}

void IECoreArnold::RendererImplementation::text( const std::string &font, const std::string &text, float kerning, const IECore::PrimitiveVariableMap &primVars )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::text", "Not implemented" );
}

void IECoreArnold::RendererImplementation::sphere( float radius, float zMin, float zMax, float thetaMax, const IECore::PrimitiveVariableMap &primVars )
{
	if( zMin != -1.0f )
	{
		msg( Msg::Warning, "IECoreArnold::RendererImplementation::sphere", "zMin not supported" );
	}
	if( zMax != 1.0f )
	{
		msg( Msg::Warning, "IECoreArnold::RendererImplementation::sphere", "zMax not supported" );
	}
	if( thetaMax != 360.0f )
	{
		msg( Msg::Warning, "IECoreArnold::RendererImplementation::sphere", "thetaMax not supported" );
	}
	
	AtNode *sphere = AiNode( "sphere" );
	AiNodeSetFlt( sphere, "radius", radius );
		
	addShape( sphere );
}

void IECoreArnold::RendererImplementation::image( const Imath::Box2i &dataWindow, const Imath::Box2i &displayWindow, const IECore::PrimitiveVariableMap &primVars )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::image", "Not implemented" );
}

void IECoreArnold::RendererImplementation::mesh( IECore::ConstIntVectorDataPtr vertsPerFace, IECore::ConstIntVectorDataPtr vertIds, const std::string &interpolation, const IECore::PrimitiveVariableMap &primVars )
{
	MeshPrimitivePtr mesh = new IECore::MeshPrimitive( vertsPerFace, vertIds, interpolation );
	mesh->variables = primVars;
	
	ToArnoldMeshConverterPtr converter = new ToArnoldMeshConverter( mesh );
	AtNode *shape = converter->convert();

	addShape( shape );
}

void IECoreArnold::RendererImplementation::nurbs( int uOrder, IECore::ConstFloatVectorDataPtr uKnot, float uMin, float uMax, int vOrder, IECore::ConstFloatVectorDataPtr vKnot, float vMin, float vMax, const IECore::PrimitiveVariableMap &primVars )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::nurbs", "Not implemented" );
}

void IECoreArnold::RendererImplementation::patchMesh( const CubicBasisf &uBasis, const CubicBasisf &vBasis, int nu, bool uPeriodic, int nv, bool vPeriodic, const PrimitiveVariableMap &primVars )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::patchMesh", "Not implemented" );
}

void IECoreArnold::RendererImplementation::geometry( const std::string &type, const CompoundDataMap &topology, const PrimitiveVariableMap &primVars )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::geometry", "Not implemented" );
}

/////////////////////////////////////////////////////////////////////////////////////////
// procedurals
/////////////////////////////////////////////////////////////////////////////////////////

int IECoreArnold::RendererImplementation::procLoader( AtProcVtable *vTable )
{
	vTable->Init = procInit;
	vTable->Cleanup = procCleanup;
	vTable->NumNodes = procNumNodes;
	vTable->GetNode = procGetNode;
	strcpy( vTable->version, AI_VERSION );
	return 1;
}

int IECoreArnold::RendererImplementation::procInit( AtNode *node, void **userPtr )
{
	ProceduralData *data = (ProceduralData *)( AiNodeGetPtr( node, "userptr" ) );
	data->procedural->render( data->renderer );
	data->procedural = 0;
	*userPtr = data;
	return 1;
}

int IECoreArnold::RendererImplementation::procCleanup( void *userPtr )
{
	ProceduralData *data = (ProceduralData *)( userPtr );
	delete data;
	return 1;
}

int IECoreArnold::RendererImplementation::procNumNodes( void *userPtr )
{
	ProceduralData *data = (ProceduralData *)( userPtr );
	return data->renderer->m_implementation->m_shapes.size();
}

AtNode* IECoreArnold::RendererImplementation::procGetNode( void *userPtr, int i )
{
	ProceduralData *data = (ProceduralData *)( userPtr );
	return data->renderer->m_implementation->m_shapes[i];
}

void IECoreArnold::RendererImplementation::procedural( IECore::Renderer::ProceduralPtr proc )
{
	Box3f bound = proc->bound();
	bound = transform( bound, m_transformStack.top() );

	AtNode *procedural = AiNode( "procedural" );
	AiNodeSetPnt( procedural, "min", bound.min.x, bound.min.y, bound.min.z );
	AiNodeSetPnt( procedural, "max", bound.max.x, bound.max.y, bound.max.z );
	
	AiNodeSetPtr( procedural, "funcptr", (AtVoid *)procLoader );
	
	ProceduralData *data = new ProceduralData;
	data->procedural = proc;
	data->renderer = new IECoreArnold::Renderer( new RendererImplementation( *this ) );
		
	AiNodeSetPtr( procedural, "userptr", data );
	
	addShape( procedural );
}

void IECoreArnold::RendererImplementation::applyTransformToNode( AtNode *node )
{
	/// \todo Make Convert.h
	const Imath::M44f &m = m_transformStack.top();
	AtMatrix mm;
	for( unsigned int i=0; i<4; i++ )
	{
		for( unsigned int j=0; j<4; j++ )
		{
			mm[i][j] = m[i][j];
		}
	}
	
	AiNodeSetMatrix( node, "matrix", mm );
}

void IECoreArnold::RendererImplementation::addShape( AtNode *shape )
{
	applyTransformToNode( shape );
	AiNodeSetPtr( shape, "shader", m_attributeStack.top().surfaceShader );
	m_shapes.push_back( shape );
}

/////////////////////////////////////////////////////////////////////////////////////////
// instancing
/////////////////////////////////////////////////////////////////////////////////////////

void IECoreArnold::RendererImplementation::instanceBegin( const std::string &name, const IECore::CompoundDataMap &parameters )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::instanceBegin", "Not implemented" );
}

void IECoreArnold::RendererImplementation::instanceEnd()
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::instanceEnd", "Not implemented" );
}

void IECoreArnold::RendererImplementation::instance( const std::string &name )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::instance", "Not implemented" );
}

/////////////////////////////////////////////////////////////////////////////////////////
// commands
/////////////////////////////////////////////////////////////////////////////////////////

IECore::DataPtr IECoreArnold::RendererImplementation::command( const std::string &name, const CompoundDataMap &parameters )
{
	msg( Msg::Warning, "IECoreArnold::RendererImplementation::command", "Not implemented" );
	return 0;
}