/*
 *  libglw, OpenGL interface
 *  Copyright (C) 2008 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <limits.h>

#include "glw.h"
#include "glw_renderer.h"
#include "fileaccess/fileaccess.h"




// #define DEBUG_SHADERS

/**
 *
 */
typedef struct render_job {
  Mtx m;
  struct glw_backend_texture *tex;
  struct glw_rgb rgb_mul;
  struct glw_rgb rgb_off;
  float alpha;
  float blur;
  int vertex_offset;
  int16_t num_vertices;
  char blendmode;
  char frontface;
  char eyespace;
} render_job_t;


/**
 *
 */
static void
prepare_delayed(glw_root_t *gr)
{
  glw_backend_root_t *gbr = &gr->gr_be;

  gbr->gbr_num_render_jobs = 0;
  gbr->gbr_vertex_offset = 0;
}

/**
 *
 */
static void
render_unlocked(glw_root_t *gr)
{
  glw_backend_root_t *gbr = &gr->gr_be;
  int i;
  struct render_job *rj = gbr->gbr_render_jobs;

  int64_t ts = showtime_get_ts();

  int current_blendmode = GLW_BLEND_NORMAL;
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  int program_switches = 0;

  for(i = 0; i < gbr->gbr_num_render_jobs; i++, rj++) {

    struct glw_backend_texture *tex = rj->tex;
    glw_program_t *gp;
    
    if(tex == NULL) {
      gp = gbr->gbr_renderer_flat;
    } else {
      
      if(rj->blur > 0.05) {
	gp = gbr->gbr_renderer_tex_blur;
      } else {
	gp = gbr->gbr_renderer_tex;
      }
      glBindTexture(gbr->gbr_primary_texture_mode, tex->tex);
    }

    if(gp == NULL)
      continue;
    
    if(glw_load_program(gbr, gp)) {
      program_switches++;
      float *vertices = gbr->gbr_vertex_buffer;

      glVertexAttribPointer(gp->gp_attribute_position,
			    3, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			    vertices);
      
      glVertexAttribPointer(gp->gp_attribute_color,
			    4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			    vertices + 5);
      
      glVertexAttribPointer(gp->gp_attribute_texcoord,
			    2, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			    vertices + 3);

      abort(); // Fix blur
    }

    glUniform4f(gp->gp_uniform_color_offset,
		rj->rgb_off.r, rj->rgb_off.g, rj->rgb_off.b, 0);
    
    glw_program_set_uniform_color(gbr, rj->rgb_mul.r, rj->rgb_mul.g,
				  rj->rgb_mul.b, rj->alpha);

    if(rj->blur > 0.05 && tex != NULL) {
      glUniform2f(gp->gp_uniform_blur_amount, 
		  1.5 * rj->blur / tex->width,
		  1.5 * rj->blur / tex->height);
    }

    if(rj->eyespace) {
      glUniformMatrix4fv(gp->gp_uniform_modelview, 1, 0, glw_identitymtx);
    } else {
      glUniformMatrix4fv(gp->gp_uniform_modelview, 1, 0, glw_mtx_get(rj->m));
    }

    if(current_blendmode != rj->blendmode) {
      current_blendmode = rj->blendmode;
      switch(current_blendmode) {
      case GLW_BLEND_NORMAL:
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	break;
	
      case GLW_BLEND_ADDITIVE:
	glBlendFunc(GL_SRC_COLOR, GL_ONE);
	break;
      }
    }
      
    glDrawArrays(GL_TRIANGLES, rj->vertex_offset, rj->num_vertices);
  }
  if(current_blendmode != GLW_BLEND_NORMAL) 
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  ts = showtime_get_ts() - ts;
  static int hold;
  
  hold++;
  if(hold < 20)
    return;

  static int cnt;
  static int64_t tssum;

  tssum += ts;
  cnt++;

  //  printf("%16lld (%lld) %d switches\n", ts, tssum/cnt, program_switches);
}


/**
 *
 */
static void
shader_render_delayed(struct glw_root *root, 
		      Mtx m,
		      struct glw_backend_texture *tex,
		      const struct glw_rgb *rgb_mul,
		      const struct glw_rgb *rgb_off,
		      float alpha, float blur,
		      const float *vertices,
		      int num_vertices,
		      const uint16_t *indices,
		      int num_triangles,
		      int flags)
{
  glw_backend_root_t *gbr = &root->gr_be;

  if(gbr->gbr_num_render_jobs >= gbr->gbr_render_jobs_capacity) {
    // Need more space
    gbr->gbr_render_jobs_capacity = 100 + gbr->gbr_render_jobs_capacity * 2; 
    gbr->gbr_render_jobs = realloc(gbr->gbr_render_jobs, 
				   sizeof(render_job_t) *
				   gbr->gbr_render_jobs_capacity);
  }

  struct render_job *rj = gbr->gbr_render_jobs + gbr->gbr_num_render_jobs;
  
  if(m == NULL) {
    rj->eyespace = 1;
  } else {
    rj->eyespace = 0;
    glw_mtx_copy(rj->m, m);
  }

  rj->tex = tex;

  switch(gbr->gbr_blendmode) {
  case GLW_BLEND_NORMAL:
    rj->rgb_mul = *rgb_mul;
    rj->alpha = alpha;
    break;

  case GLW_BLEND_ADDITIVE:
    rj->rgb_mul.r = rgb_mul->r * alpha;
    rj->rgb_mul.g = rgb_mul->g * alpha;
    rj->rgb_mul.b = rgb_mul->b * alpha;
    rj->alpha = 1;
    break;
  }

  if(rgb_off != NULL)
    rj->rgb_off = *rgb_off;
  else {
    rj->rgb_off.r = 0;
    rj->rgb_off.g = 0;
    rj->rgb_off.b = 0;
  }

  rj->blur = blur;
  rj->blendmode = gbr->gbr_blendmode;
  rj->frontface = gbr->gbr_frontface;

  int vnum = indices ? num_triangles * 3 : num_vertices;

  if(gbr->gbr_vertex_offset + vnum > gbr->gbr_vertex_buffer_capacity) {
    gbr->gbr_vertex_buffer_capacity = 100 + vnum +
      gbr->gbr_vertex_buffer_capacity * 2;

    gbr->gbr_vertex_buffer = realloc(gbr->gbr_vertex_buffer,
				     sizeof(float) * VERTEX_SIZE * 
				     gbr->gbr_vertex_buffer_capacity);
  }

  float *vdst = gbr->gbr_vertex_buffer + gbr->gbr_vertex_offset * VERTEX_SIZE;

  if(indices != NULL) {
    int i;
    for(i = 0; i < num_triangles * 3; i++) {
      const float *v = &vertices[indices[i] * VERTEX_SIZE];
      memcpy(vdst, v, VERTEX_SIZE * sizeof(float));
      vdst += VERTEX_SIZE;
    }
  } else {
    memcpy(vdst, vertices, num_vertices * VERTEX_SIZE * sizeof(float));
  }

  rj->vertex_offset = gbr->gbr_vertex_offset;
  rj->num_vertices = vnum;
  gbr->gbr_vertex_offset += vnum;
  gbr->gbr_num_render_jobs++;
}



/**
 * Render function using OpenGL shaders
 */
static void
shader_render(struct glw_root *root, 
	      Mtx m,
	      struct glw_backend_texture *tex,
	      const struct glw_rgb *rgb_mul,
	      const struct glw_rgb *rgb_off,
	      float alpha, float blur,
	      const float *vertices,
	      int num_vertices,
	      const uint16_t *indices,
	      int num_triangles,
	      int flags)
{
  glw_backend_root_t *gbr = &root->gr_be;
  glw_program_t *gp;

  if(tex == NULL) {
    gp = gbr->gbr_renderer_flat;
  } else {
    
    if(blur > 0.05 || 1) {
      gp = gbr->gbr_renderer_tex_blur;
    } else {
      gp = gbr->gbr_renderer_tex;
    }
    glBindTexture(gbr->gbr_primary_texture_mode, tex->tex);
  }

  if(gp == NULL)
    return;

  glw_load_program(gbr, gp);

  if(rgb_off != NULL)
    glUniform4f(gp->gp_uniform_color_offset,
		rgb_off->r, rgb_off->g, rgb_off->b, 0);
  else
    glUniform4f(gp->gp_uniform_color_offset, 0,0,0,0);

  switch(gbr->gbr_blendmode) {
  case GLW_BLEND_NORMAL:
    glw_program_set_uniform_color(gbr, rgb_mul->r, rgb_mul->g, rgb_mul->b,
				  alpha);
    break;
  case GLW_BLEND_ADDITIVE:
    glw_program_set_uniform_color(gbr, 
				  rgb_mul->r * alpha,
				  rgb_mul->g * alpha,
				  rgb_mul->b * alpha,
				  1);
    break;
  }

#if 0
  if(blur > 0.05 && tex != NULL) {
    glUniform2f(gp->gp_uniform_blur_amount, 
		1.5 * blur / tex->width,
		1.5 * blur / tex->height);
  }
#endif

  if(tex != NULL) {
    glUniform2f(gp->gp_uniform_blur_amount, 
		1.5 / tex->width,
		1.5 / tex->height);
  }

  glUniformMatrix4fv(gp->gp_uniform_modelview, 1, 0,
		     glw_mtx_get(m) ?: glw_identitymtx);

  glVertexAttribPointer(gp->gp_attribute_position,
			3, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			vertices);

  glVertexAttribPointer(gp->gp_attribute_color,
			4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			vertices + 5);

  if(gp->gp_attribute_texcoord != -1)
    glVertexAttribPointer(gp->gp_attribute_texcoord,
			  2, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			  vertices + 3);

  if(gp->gp_attribute_blur != -1)
    glVertexAttribPointer(gp->gp_attribute_blur,
			  1, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			  vertices + 9);
  
  if(indices != NULL)
    glDrawElements(GL_TRIANGLES, num_triangles * 3, GL_UNSIGNED_SHORT, indices);
  else
    glDrawArrays(GL_TRIANGLES, 0, num_vertices);
}



/**
 *
 */
GLuint
glw_compile_shader(const char *filename, int type)
{
  char *src;
  GLint v, len;
  GLuint s;
  char path[PATH_MAX];
  char log[4096];
  
  snprintf(path, sizeof(path), "%s/src/ui/glw/glsl/%s", 
	   showtime_dataroot(), filename);
  if((src = fa_load(path, NULL, NULL, log, sizeof(log), NULL, 0,
		    NULL, NULL)) == NULL) {
    TRACE(TRACE_ERROR, "glw", "Unable to load shader %s -- %s",
	  path, log);
    return 0;
  }
  
  s = glCreateShader(type);
  glShaderSource(s, 1, (const char **)&src, NULL);
  
  glCompileShader(s);
  glGetShaderInfoLog(s, sizeof(log), &len, log); 
  glGetShaderiv(s, GL_COMPILE_STATUS, &v);
    
  free(src);

  if(!v) {
    TRACE(TRACE_ERROR, "GLW", "Unable to compile shader %s", path);
    TRACE(TRACE_ERROR, "GLW", "%s", log);
    return 0;
  }
  return s;
}


/**
 *
 */
glw_program_t *
glw_make_program(glw_backend_root_t *gbr, const char *title,
		 GLuint vs, GLuint fs)
{
  char log[4096];
  GLsizei len;
  GLint v;
  GLuint p;
  int i;

  p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);

  glGetProgramInfoLog(p, sizeof(log), &len, log); 

  glGetProgramiv(p, GL_LINK_STATUS, &v);
  if(!v) {
    TRACE(TRACE_ERROR, "GLW", "Unable to link shader %s", title);
    TRACE(TRACE_ERROR, "GLW", "%s", log);
    return NULL;
  }

  glw_program_t *gp = calloc(1, sizeof(glw_program_t));

  gp->gp_title = strdup(title);
  gp->gp_program = p;

  glUseProgram(p);
  gbr->gbr_current = gp;

  gp->gp_attribute_position = glGetAttribLocation(p, "a_position");
  gp->gp_attribute_texcoord = glGetAttribLocation(p, "a_texcoord");
  gp->gp_attribute_color    = glGetAttribLocation(p, "a_color");
  gp->gp_attribute_blur     = glGetAttribLocation(p, "a_blur");

  gp->gp_uniform_modelview  = glGetUniformLocation(p, "u_modelview");
  gp->gp_uniform_color      = glGetUniformLocation(p, "u_color");
  gp->gp_uniform_colormtx   = glGetUniformLocation(p, "u_colormtx");
  gp->gp_uniform_blend      = glGetUniformLocation(p, "u_blend");
  gp->gp_uniform_color_offset= glGetUniformLocation(p, "u_color_offset");
  gp->gp_uniform_blur_amount = glGetUniformLocation(p, "u_blur_amount");
  
#ifdef DEBUG_SHADERS
  printf("Loaded %s\n", title);
  printf("  a_position     = %d\n", gp->gp_attribute_position);
  printf("  a_texcoord     = %d\n", gp->gp_attribute_texcoord);
  printf("  a_color        = %d\n", gp->gp_attribute_color);

  printf("  u_modelview = %d\n", gp->gp_uniform_modelview);
  printf("  u_color     = %d\n", gp->gp_uniform_color);
  printf("  u_colormtx  = %d\n", gp->gp_uniform_colormtx);
  printf("  u_blend     = %d\n", gp->gp_uniform_blend);
  printf("  u_color_offset = %d\n", gp->gp_uniform_color_offset);
#endif

  for(i = 0; i < 6; i++) {
    char name[8];
    snprintf(name, sizeof(name), "u_t%d", i);
    gp->gp_uniform_t[i]         = glGetUniformLocation(p, name);
    if(gp->gp_uniform_t[i] != -1)
      glUniform1i(gp->gp_uniform_t[i], i);
#ifdef DEBUG_SHADERS
    printf("  u_t%d       = %d\n", i, gp->gp_uniform_t[i]);
#endif
  }

  return gp;
}


/**
 *
 */
int
glw_load_program(glw_backend_root_t *gbr, glw_program_t *gp)
{
  if(gbr->gbr_current == gp)
    return 0;

  if(gbr->gbr_current != NULL) {
    glw_program_t *old = gbr->gbr_current;
    if(old->gp_attribute_position != -1)
      glDisableVertexAttribArray(old->gp_attribute_position);
    if(old->gp_attribute_texcoord != -1)
      glDisableVertexAttribArray(old->gp_attribute_texcoord);
    if(old->gp_attribute_color != -1)
      glDisableVertexAttribArray(old->gp_attribute_color);
    if(old->gp_attribute_blur != -1)
      glDisableVertexAttribArray(old->gp_attribute_blur);
  }

  gbr->gbr_current = gp;

  if(gp == NULL) {
    glUseProgram(0);
    return 1;
  }

  glUseProgram(gp->gp_program);

  if(gp->gp_attribute_position != -1)
      glEnableVertexAttribArray(gp->gp_attribute_position);
  if(gp->gp_attribute_texcoord != -1)
    glEnableVertexAttribArray(gp->gp_attribute_texcoord);
  if(gp->gp_attribute_color != -1)
    glEnableVertexAttribArray(gp->gp_attribute_color);
  if(gp->gp_attribute_blur != -1)
    glEnableVertexAttribArray(gp->gp_attribute_blur);
  return 1;
}



/**
 *
 */
void
glw_program_set_modelview(glw_backend_root_t *gbr, glw_rctx_t *rc)
{
  const float *m = rc ? glw_mtx_get(rc->rc_mtx) : glw_identitymtx;
  glUniformMatrix4fv(gbr->gbr_current->gp_uniform_modelview, 1, 0, m);
}

/**
 *
 */
void
glw_program_set_uniform_color(glw_backend_root_t *gbr,
			      float r, float g, float b, float a)
{
  glUniform4f(gbr->gbr_current->gp_uniform_color, r, g, b, a);
}



/**
 *
 */
int
glw_opengl_shaders_init(glw_root_t *gr)
{
  glw_backend_root_t *gbr = &gr->gr_be;

  GLuint vs, fs;

  vs = glw_compile_shader("v1.glsl", GL_VERTEX_SHADER);

  fs = glw_compile_shader("f_tex.glsl", GL_FRAGMENT_SHADER);
  gbr->gbr_renderer_tex = glw_make_program(gbr, "Texture", vs, fs);
  glDeleteShader(fs);

  fs = glw_compile_shader("f_tex_blur.glsl", GL_FRAGMENT_SHADER);
  gbr->gbr_renderer_tex_blur = glw_make_program(gbr, "Texture", vs, fs);
  glDeleteShader(fs);

  fs = glw_compile_shader("f_flat.glsl", GL_FRAGMENT_SHADER);
  gbr->gbr_renderer_flat = glw_make_program(gbr, "Flat", vs, fs);
  glDeleteShader(fs);

  glDeleteShader(vs);

  //    gbr->gbr_renderer_draw = glw_renderer_shader;


  // Video renderer

  vs = glw_compile_shader("yuv2rgb_v.glsl", GL_VERTEX_SHADER);


  fs = glw_compile_shader("yuv2rgb_1f_norm.glsl", GL_FRAGMENT_SHADER);
  gbr->gbr_yuv2rgb_1f = glw_make_program(gbr, "yuv2rgb_1f_norm", vs, fs);
  glDeleteShader(fs);

  fs = glw_compile_shader("yuv2rgb_2f_norm.glsl", GL_FRAGMENT_SHADER);
  gbr->gbr_yuv2rgb_2f = glw_make_program(gbr, "yuv2rgb_2f_norm", vs, fs);
  glDeleteShader(fs);

  glDeleteShader(vs);

  if(1) {
    gr->gr_render = shader_render;
  } else {
    gr->gr_render = shader_render_delayed;
    gr->gr_be_prepare = prepare_delayed;
    gr->gr_be_render_unlocked = render_unlocked;
    gbr->gbr_delayed_rendering = 1;
    
  }

  prop_set_string(prop_create(gr->gr_uii.uii_prop, "rendermode"),
		  "OpenGL VP/FP shaders");
  return 0;
}
