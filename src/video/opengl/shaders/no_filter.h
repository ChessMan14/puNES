{
"#if defined(VERTEX)\n"
"attribute vec2 VertexCoord;\n"
"attribute vec4 Color;\n"
"attribute vec2 TexCoord;\n"
"uniform mat4 MVPMatrix;\n"
"varying vec2 tex_coord;\n"
"void main() {\n"
"   gl_Position = MVPMatrix * vec4(VertexCoord, 0.0, 1.0);\n"
"   tex_coord = TexCoord;\n"
"}\n"
"#elif defined(FRAGMENT)\n"
"#ifdef GL_ES\n"
"precision mediump float;\n"
"#endif\n"
"uniform sampler2D Texture;\n"
"varying vec2 tex_coord;\n"
"void main() {\n"
"   gl_FragColor = vec4(texture2D(Texture, tex_coord).rgb, 1.0);\n"
"}\n"
"#endif\n"
},
