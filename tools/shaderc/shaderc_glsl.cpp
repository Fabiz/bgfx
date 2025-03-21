/*
 * Copyright 2011-2023 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "shaderc.h"
#include "glsl_optimizer.h"
#include <regex> // added by fso


namespace bgfx { namespace glsl
{
	static bool compile(const Options& _options, uint32_t _version, const std::string& _code, bx::WriterI* _shaderWriter, bx::WriterI* _messageWriter)
	{
		bx::ErrorAssert messageErr;

		char ch = _options.shaderType;
		const glslopt_shader_type type = ch == 'f'
			? kGlslOptShaderFragment
			: (ch == 'c' ? kGlslOptShaderCompute : kGlslOptShaderVertex);

		glslopt_target target = kGlslTargetOpenGL;
		if(_version == BX_MAKEFOURCC('M', 'T', 'L', 0))
		{
			target = kGlslTargetMetal;
		} else if(_version < 0x80000000) {
			target = kGlslTargetOpenGL;
		}
		else {
			_version &= ~0x80000000;
			target = (_version >= 300) ? kGlslTargetOpenGLES30 : kGlslTargetOpenGLES20;
		}

		glslopt_ctx* ctx = glslopt_initialize(target);

		glslopt_shader* shader = glslopt_optimize(ctx, type, _code.c_str(), 0);

		if (!glslopt_get_status(shader) )
		{
			const char* log = glslopt_get_log(shader);
			int32_t source  = 0;
			int32_t line    = 0;
			int32_t column  = 0;
			int32_t start   = 0;
			int32_t end     = INT32_MAX;

			bool found = false
				|| 3 == sscanf(log, "%u:%u(%u):", &source, &line, &column)
				|| 2 == sscanf(log, "(%u,%u):", &line, &column)
				;

			if (found
			&&  0 != line)
			{
				start = bx::uint32_imax(1, line-10);
				end   = start + 20;
			}

			printCode(_code.c_str(), line, start, end, column);
			bx::write(_messageWriter, &messageErr, "Error: %s\n", log);
			glslopt_shader_delete(shader);
			glslopt_cleanup(ctx);
			return false;
		}

		const char* optimizedShader = glslopt_get_output(shader);

		std::string out;
		// Trim all directives.
		while ('#' == *optimizedShader)
		{
			optimizedShader = bx::strFindNl(optimizedShader).getPtr();
		}

		out.append(optimizedShader, strlen(optimizedShader));
		optimizedShader = out.c_str();

		{
			char* code = const_cast<char*>(optimizedShader);
			strReplace(code, "gl_FragDepthEXT", "gl_FragDepth");

			strReplace(code, "textureLodEXT", "texture2DLod");
			strReplace(code, "textureGradEXT", "texture2DGrad");

			strReplace(code, "texture2DLodARB", "texture2DLod");
			strReplace(code, "texture2DLodEXT", "texture2DLod");
			strReplace(code, "texture2DGradARB", "texture2DGrad");
			strReplace(code, "texture2DGradEXT", "texture2DGrad");

			strReplace(code, "textureCubeLodARB", "textureCubeLod");
			strReplace(code, "textureCubeLodEXT", "textureCubeLod");
			strReplace(code, "textureCubeGradARB", "textureCubeGrad");
			strReplace(code, "textureCubeGradEXT", "textureCubeGrad");

			strReplace(code, "texture2DProjLodARB", "texture2DProjLod");
			strReplace(code, "texture2DProjLodEXT", "texture2DProjLod");
			strReplace(code, "texture2DProjGradARB", "texture2DProjGrad");
			strReplace(code, "texture2DProjGradEXT", "texture2DProjGrad");

			strReplace(code, "shadow2DARB", "shadow2D");
			strReplace(code, "shadow2DEXT", "shadow2D");
			strReplace(code, "shadow2DProjARB", "shadow2DProj");
			strReplace(code, "shadow2DProjEXT", "shadow2DProj");
		}

		UniformArray uniforms;
		
		// added by fso: make sure the vertex shader is always highp 
	    std::string shadercodetemp; 
  		if (target != kGlslTargetMetal)
	  	{
			if (_options.shaderType == 'v') {
				shadercodetemp.assign(optimizedShader);
				// its much simpler to use std::string for a replacement than the bx functions
				shadercodetemp = std::regex_replace(shadercodetemp, std::regex("lowp"), "highp");
				shadercodetemp = std::regex_replace(shadercodetemp, std::regex("mediump"), "highp");
				optimizedShader = shadercodetemp.c_str();
			}
		} 
		// added by fso: end

		if (target != kGlslTargetMetal)
		{
			bx::StringView parse(optimizedShader);

			while (!parse.isEmpty() )
			{
				parse = bx::strLTrimSpace(parse);
				bx::StringView eol = bx::strFind(parse, ';');
				if (!eol.isEmpty() )
				{
					bx::StringView qualifier = nextWord(parse);

					if (0 == bx::strCmp(qualifier, "precision", 9) )
					{
						// skip precision
						parse.set(eol.getPtr() + 1, parse.getTerm() );
						continue;
					}

					if (0 == bx::strCmp(qualifier, "attribute", 9)
					||  0 == bx::strCmp(qualifier, "varying",   7)
					||  0 == bx::strCmp(qualifier, "in",        2)
					||  0 == bx::strCmp(qualifier, "out",       3)
					   )
					{
						// skip attributes and varyings.
						parse.set(eol.getPtr() + 1, parse.getTerm() );
						continue;
					}

					if (0 == bx::strCmp(qualifier, "flat", 4)
					||  0 == bx::strCmp(qualifier, "smooth", 6)
					||  0 == bx::strCmp(qualifier, "noperspective", 13)
					||  0 == bx::strCmp(qualifier, "centroid", 8)
					   )
					{
						// skip interpolation qualifiers
						parse.set(eol.getPtr() + 1, parse.getTerm() );
						continue;
					}

					if (0 == bx::strCmp(parse, "tmpvar", 6) )
					{
						// skip temporaries
						parse.set(eol.getPtr() + 1, parse.getTerm() );
						continue;
					}

					if (0 != bx::strCmp(qualifier, "uniform", 7) )
					{
						// end if there is no uniform keyword.
						parse.clear();
						continue;
					}

					bx::StringView precision;
					bx::StringView typen = nextWord(parse);

					if (0 == bx::strCmp(typen, "lowp", 4)
					||  0 == bx::strCmp(typen, "mediump", 7)
					||  0 == bx::strCmp(typen, "highp", 5) )
					{
						precision = typen;
						typen = nextWord(parse);
					}

					BX_UNUSED(precision);

					char uniformType[256];

					if (0 == bx::strCmp(typen, "sampler", 7)
					||  0 == bx::strCmp(typen, "isampler", 8)
					||  0 == bx::strCmp(typen, "usampler", 8) )
					{
						bx::strCopy(uniformType, BX_COUNTOF(uniformType), "int");
					}
					else
					{
						bx::strCopy(uniformType, BX_COUNTOF(uniformType), typen);
					}

					bx::StringView name = nextWord(parse);

					uint8_t num = 1;
					bx::StringView array = bx::strSubstr(parse, 0, 1);
					if (0 == bx::strCmp(array, "[", 1) )
					{
						parse = bx::strLTrimSpace(bx::StringView(parse.getPtr() + 1, parse.getTerm() ) );

						uint32_t tmp;
						bx::fromString(&tmp, parse);
						num = uint8_t(tmp);
					}

					Uniform un;
					un.type = nameToUniformTypeEnum(uniformType);

					if (UniformType::Count != un.type)
					{
						un.name.assign(name.getPtr(), name.getTerm());

						BX_TRACE("name: %s (type %d, num %d)", un.name.c_str(), un.type, num);

						un.num = num;
						un.regIndex = 0;
						un.regCount = num;
						switch (un.type)
						{
						case UniformType::Mat3:
							un.regCount *= 3;
							break;
						case UniformType::Mat4:
							un.regCount *= 4;
							break;
						default:
							break;
						}

						uniforms.push_back(un);
					}

					parse = bx::strLTrimSpace(bx::strFindNl(bx::StringView(eol.getPtr(), parse.getTerm() ) ) );
				}
			}
		}
		else
		{
			const bx::StringView optShader(optimizedShader);
			bx::StringView parse = bx::strFind(optimizedShader, "struct xlatMtlShaderUniform {");
			bx::StringView end   = parse;
			if (!parse.isEmpty() )
			{
				parse.set(parse.getPtr() + bx::strLen("struct xlatMtlShaderUniform {"), optShader.getTerm() );
				end = bx::strFind(parse, "};");
			}

			while ( parse.getPtr() < end.getPtr()
			&&     !parse.isEmpty() )
			{
				parse.set(bx::strLTrimSpace(parse).getPtr(), optShader.getTerm() );
				const bx::StringView eol = bx::strFind(parse, ';');
				if (!eol.isEmpty() )
				{
					const char* typen = parse.getPtr();

					char uniformType[256];
					parse = bx::strWord(parse);
					bx::strCopy(uniformType, parse.getLength()+1, typen);
					parse.set(parse.getPtr()+parse.getLength(),optShader.getTerm());
					const char* name = bx::strLTrimSpace(parse).getPtr();
					parse.set(name, optShader.getTerm() );

					char uniformName[256];
					uint8_t num = 1;
					bx::StringView array = bx::strFind(bx::StringView(name, int32_t(eol.getPtr()-parse.getPtr() ) ), "[");
					if (!array.isEmpty() )
					{
						bx::strCopy(uniformName, int32_t(array.getPtr()-name+1), name);

						char arraySize[32];
						bx::StringView arrayEnd = bx::strFind(bx::StringView(array.getPtr(), int32_t(eol.getPtr()-array.getPtr() ) ), "]");
						bx::strCopy(arraySize, int32_t(arrayEnd.getPtr()-array.getPtr() ), array.getPtr()+1);
						uint32_t tmp;
						bx::fromString(&tmp, arraySize);
						num = uint8_t(tmp);
					}
					else
					{
						bx::strCopy(uniformName, int32_t(eol.getPtr()-name+1), name);
					}

					Uniform un;
					un.type = nameToUniformTypeEnum(uniformType);

					if (UniformType::Count != un.type)
					{
						BX_TRACE("name: %s (type %d, num %d)", uniformName, un.type, num);

						un.name = uniformName;
						un.num = num;
						un.regIndex = 0;
						un.regCount = num;
						uniforms.push_back(un);
					}

					parse = eol.getPtr() + 1;
				}
			}

			bx::StringView mainEntry("xlatMtlShaderOutput xlatMtlMain (");
			parse = bx::strFind(optimizedShader, mainEntry);
			end = parse;
			if (!parse.isEmpty())
			{
				parse.set(parse.getPtr() + mainEntry.getLength(), optShader.getTerm());
				end = bx::strFind(parse, "{");
			}

			while (parse.getPtr() < end.getPtr()
				&& !parse.isEmpty())
			{
				parse.set(bx::strLTrimSpace(parse).getPtr(), optShader.getTerm());
				const bx::StringView textureNameMark("[[texture(");
				const bx::StringView textureName = bx::strFind(parse, textureNameMark);

				if (!textureName.isEmpty())
				{
					Uniform un;
					un.type = nameToUniformTypeEnum("int");	// int for sampler
					const char* varNameEnd = textureName.getPtr() - 1;
					parse.set(parse.getPtr(), varNameEnd - 1);
					const char* varNameBeg = parse.getPtr();
					for (int ii = parse.getLength() - 1; 0 <= ii; --ii)
					{
						if (varNameBeg[ii] == ' ')
						{
							parse.set(varNameBeg + ii + 1, varNameEnd);
							break;
						}
					}
					char uniformName[256];
					bx::strCopy(uniformName, parse.getLength() + 1, parse);
					un.name = uniformName;
					const char* regIndexBeg = textureName.getPtr() + textureNameMark.getLength();
					bx::StringView regIndex = bx::strFind(regIndexBeg, ")");

					regIndex.set(regIndexBeg, regIndex.getPtr());
					uint32_t tmp;
					bx::fromString(&tmp, regIndex);
					un.regIndex = uint16_t(tmp);
					un.num = 1;
					un.regCount = 1;

					uniforms.push_back(un);

					parse = regIndex.getPtr() + 1;
				}
				else
				{
					parse = textureName;
				}
			}
		}

		bx::ErrorAssert err;

		uint16_t count = (uint16_t)uniforms.size();
		bx::write(_shaderWriter, count, &err);

		for (UniformArray::const_iterator it = uniforms.begin(); it != uniforms.end(); ++it)
		{
			const Uniform& un = *it;
			uint8_t nameSize = (uint8_t)un.name.size();
			bx::write(_shaderWriter, nameSize, &err);
			bx::write(_shaderWriter, un.name.c_str(), nameSize, &err);
			uint8_t uniformType = uint8_t(un.type);
			bx::write(_shaderWriter, uniformType, &err);
			bx::write(_shaderWriter, un.num, &err);
			bx::write(_shaderWriter, un.regIndex, &err);
			bx::write(_shaderWriter, un.regCount, &err);
			bx::write(_shaderWriter, un.texComponent, &err);
			bx::write(_shaderWriter, un.texDimension, &err);
			bx::write(_shaderWriter, un.texFormat, &err);

			BX_TRACE("%s, %s, %d, %d, %d"
				, un.name.c_str()
				, getUniformTypeName(un.type)
				, un.num
				, un.regIndex
				, un.regCount
				);
		}

		uint32_t shaderSize = (uint32_t)bx::strLen(optimizedShader);
		bx::write(_shaderWriter, shaderSize, &err);
		bx::write(_shaderWriter, optimizedShader, shaderSize, &err);
		uint8_t nul = 0;
		bx::write(_shaderWriter, nul, &err);

		if (_options.disasm )
		{
			std::string disasmfp = _options.outputFilePath + ".disasm";
			writeFile(disasmfp.c_str(), optimizedShader, shaderSize);
		}

		glslopt_shader_delete(shader);
		glslopt_cleanup(ctx);

		return true;
	}

} // namespace glsl

	bool compileGLSLShader(const Options& _options, uint32_t _version, const std::string& _code, bx::WriterI* _shaderWriter, bx::WriterI* _messageWriter)
	{
		return glsl::compile(_options, _version, _code, _shaderWriter, _messageWriter);
	}

} // namespace bgfx
