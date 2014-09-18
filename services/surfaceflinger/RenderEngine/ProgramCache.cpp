/*
 * Copyright 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <utils/String8.h>

#include "ProgramCache.h"
#include "Program.h"
#include "Description.h"

namespace android {
// -----------------------------------------------------------------------------------------------


/*
 * A simple formatter class to automatically add the endl and
 * manage the indentation.
 */

class Formatter;
static Formatter& indent(Formatter& f);
static Formatter& dedent(Formatter& f);

class Formatter {
    String8 mString;
    int mIndent;
    typedef Formatter& (*FormaterManipFunc)(Formatter&);
    friend Formatter& indent(Formatter& f);
    friend Formatter& dedent(Formatter& f);
public:
    Formatter() : mIndent(0) {}

    String8 getString() const {
        return mString;
    }

    friend Formatter& operator << (Formatter& out, const char* in) {
        for (int i=0 ; i<out.mIndent ; i++) {
            out.mString.append("    ");
        }
        out.mString.append(in);
        out.mString.append("\n");
        return out;
    }
    friend inline Formatter& operator << (Formatter& out, const String8& in) {
        return operator << (out, in.string());
    }
    friend inline Formatter& operator<<(Formatter& to, FormaterManipFunc func) {
        return (*func)(to);
    }
};
Formatter& indent(Formatter& f) {
    f.mIndent++;
    return f;
}
Formatter& dedent(Formatter& f) {
    f.mIndent--;
    return f;
}

// -----------------------------------------------------------------------------------------------

ANDROID_SINGLETON_STATIC_INSTANCE(ProgramCache)


ProgramCache::ProgramCache() {
}

ProgramCache::~ProgramCache() {
}

ProgramCache::Key ProgramCache::computeKey(const Description& description) {
    Key needs;
    needs.set(Key::TEXTURE_MASK,
            !description.mTextureEnabled ? Key::TEXTURE_OFF :
            description.mTexture.getTextureTarget() == GL_TEXTURE_EXTERNAL_OES ? Key::TEXTURE_EXT :
            description.mTexture.getTextureTarget() == GL_TEXTURE_2D           ? Key::TEXTURE_2D :
            Key::TEXTURE_OFF)
    .set(Key::PLANE_ALPHA_MASK,
            (description.mPlaneAlpha < 1) ? Key::PLANE_ALPHA_LT_ONE : Key::PLANE_ALPHA_EQ_ONE)
    .set(Key::BLEND_MASK,
            description.mPremultipliedAlpha ? Key::BLEND_PREMULT : Key::BLEND_NORMAL)
    .set(Key::OPACITY_MASK,
            description.mOpaque ? Key::OPACITY_OPAQUE : Key::OPACITY_TRANSLUCENT)
    .set(Key::COLOR_MATRIX_MASK,
            description.mColorMatrixEnabled ? Key::COLOR_MATRIX_ON :  Key::COLOR_MATRIX_OFF)
    .set(Key::SBS_MASK,
	    description.mSBSEnabled ? Key::SBS_ON : Key::SBS_OFF)
    .set(Key::DIST_MASK,
	    description.mDistEnabled ? Key::DIST_ON : Key::DIST_OFF);
    return needs;
}

String8 ProgramCache::generateVertexShader(const Key& needs) {
    Formatter vs;
    if (needs.isTexturing()) {
        vs  << "attribute vec4 texCoords;"
            << "varying vec2 outTexCoords;";
    }
    if(needs.hasSBSEnabled()) {
      vs << "varying vec3 fragpos1;"
	 << "varying vec3 fragpos2;"
	/* mat3(2.0/SIZE_X, 0.0, -1.0-2.0*OFFSET1_X/SIZE_X, 0.0, 2.0/SIZE_Y, -1.0-2.0*OFFSET1_Y/SIZE_Y, 0.0, 0.0, 0.0);"
	 * mat3(2.0/SIZE_X, 0.0, -1.0-2.0*OFFSET2_X/SIZE_X, 0.0, 2.0/SIZE_Y, -1.0-2.0*OFFSET2_Y/SIZE_Y, 0.0, 0.0, 0.0);"
	 */
	 << "uniform mat3 win1m;" //  = mat3(6.0,0.0,-3,   0.0,6.0,-1.0,    0.0,0.0,0.0);"
	 << "uniform mat3 win2m;" //  = mat3(6.0,0.0,-3,   0.0,6.0,-5.0,    0.0,0.0,0.0);"
	;
    }
    vs << "attribute vec4 position;"
       << "uniform mat4 projection;"
       << "uniform mat4 texture;"
       << "void main(void) {" << indent
       << "gl_Position = projection * position;";
    if (needs.isTexturing()) {
        vs << "outTexCoords = (texture * texCoords).st;";
    }
    if (needs.hasSBSEnabled()) {
	ALOGD("Create vertex shader with sbs");
	vs << "vec2 x = texCoords.xy;"
	   << "fragpos1 = (vec3(x,1.0)*win1m+1.0)/2.0;"
	   << "fragpos2 = (vec3(x,1.0)*win2m+1.0)/2.0;"
	  ;
    }
    vs << dedent << "}";
    return vs.getString();
}

String8 ProgramCache::generateFragmentShader(const Key& needs) {
    Formatter fs;
    ALOGD("XXXXXX hasSBSEnabled is %d", (int)needs.hasSBSEnabled());

    if (needs.getTextureTarget() == Key::TEXTURE_EXT) {
        fs << "#extension GL_OES_EGL_image_external : require";
    }

    // default precision is required-ish in fragment shaders
    fs << "precision mediump float;";

    if (needs.getTextureTarget() == Key::TEXTURE_EXT) {
        fs << "uniform samplerExternalOES sampler;"
           << "varying vec2 outTexCoords;";
    } else if (needs.getTextureTarget() == Key::TEXTURE_2D) {
        fs << "uniform sampler2D sampler;"
           << "varying vec2 outTexCoords;";
    } else if (needs.getTextureTarget() == Key::TEXTURE_OFF) {
        fs << "uniform vec4 color;";
    }
    if (needs.hasPlaneAlpha()) {
        fs << "uniform float alphaPlane;";
    }
    if (needs.hasColorMatrix()) {
        fs << "uniform mat4 colorMatrix;";
    }
    if(needs.hasSBSEnabled()) {
	ALOGD("Create fragment shader with sbs");
        fs << "varying vec3 fragpos1;"
	   << "varying vec3 fragpos2;"
	   << "uniform vec4 distortParam;" //  = vec4(1.0,-0.42,0.24,0.0);"
	   << "vec2 Distort(vec2 pa) {"
	   << "   vec2 p = 2.0*pa - 1.0;"
	   << "   p = clamp(p, vec2(-1.1), vec2(1.1));"
	   << "   float rSq = p.x*p.x*3.16+p.y*p.y;" // (16/9)^2 = 3.16
	   << "   return p * (distortParam.x + distortParam.y*rSq + distortParam.z*rSq*rSq + distortParam.w*rSq*rSq*rSq)/2.0 + 0.5;"
	   << "}";

    }
    fs << "void main(void) {" << indent;
    
    if (needs.isTexturing()) {
      if(needs.hasSBSEnabled())  {
	fs << "gl_FragColor=vec4(0.0);"
	   << "vec2 pos = vec2(-1.0);"
	   << "if(fragpos1.x >= 0.0 && fragpos1.x <= 1.0 && fragpos1.y >= 0.0 && fragpos1.y <= 1.0)"
	   << "   pos = Distort(fragpos1.xy);"
	   << "if(fragpos2.x >= 0.0 && fragpos2.x <= 1.0 && fragpos2.y >= 0.0 && fragpos2.y <= 1.0)"
	   << "   pos = Distort(fragpos2.xy);"	  
	   << "if(pos.x >= 0.0 && pos.x <= 1.0 && pos.y >= 0.0 && pos.y <= 1.0)"
	   << "   gl_FragColor = texture2D(sampler, pos);"
	  ;
      }
      else {
	fs << "gl_FragColor = texture2D(sampler, outTexCoords);";
      }
    } else {
        fs << "gl_FragColor = color;";
    }
    if (needs.isOpaque()) {
        fs << "gl_FragColor.a = 1.0;";
    }
    if (needs.hasPlaneAlpha()) {
        // modulate the alpha value with planeAlpha
        if (needs.isPremultiplied()) {
            // ... and the color too if we're premultiplied
            fs << "gl_FragColor *= alphaPlane;";
        } else {
            fs << "gl_FragColor.a *= alphaPlane;";
        }
    }

    if (needs.hasColorMatrix()) {
        if (!needs.isOpaque() && needs.isPremultiplied()) {
            // un-premultiply if needed before linearization
            fs << "gl_FragColor.rgb = gl_FragColor.rgb/gl_FragColor.a;";
        }
        fs << "gl_FragColor.rgb = pow(gl_FragColor.rgb, vec3(2.2));";
        fs << "gl_FragColor     = colorMatrix*gl_FragColor;";
        fs << "gl_FragColor.rgb = pow(gl_FragColor.rgb, vec3(1.0 / 2.2));";
        if (!needs.isOpaque() && needs.isPremultiplied()) {
            // and re-premultiply if needed after gamma correction
            fs << "gl_FragColor.rgb = gl_FragColor.rgb*gl_FragColor.a;";
        }
    }

    fs << dedent << "}";
    //ALOGD("shader: ");
    return fs.getString();
}

Program* ProgramCache::generateProgram(const Key& needs) {
    // vertex shader
    String8 vs = generateVertexShader(needs);

    // fragment shader
    String8 fs = generateFragmentShader(needs);

    Program* program = new Program(needs, vs.string(), fs.string());
    return program;
}

void ProgramCache::useProgram(const Description& description) {

    // generate the key for the shader based on the description
    Key needs(computeKey(description));

     // look-up the program in the cache
    Program* program = mCache.valueFor(needs);
    if (program == NULL) {
        // we didn't find our program, so generate one...
        nsecs_t time = -systemTime();
        program = generateProgram(needs);
        mCache.add(needs, program);
        time += systemTime();

        //ALOGD(">>> generated new program: needs=%08X, time=%u ms (%d programs)",
        //        needs.mNeeds, uint32_t(ns2ms(time)), mCache.size());
    }

    // here we have a suitable program for this description
    if (program->isValid()) {
        program->use();
        program->setUniforms(description);
    }
}


} /* namespace android */
