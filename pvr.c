/*
 * pvr plug-in version 0.00
 */
#include <glib.h>
#include <setjmp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef _O_BINARY
#define _O_BINARY 0
#endif

#include <gtk/gtk.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

gint run_flag = FALSE;

#define MODE_ARGB1555 0x00
#define MODE_RGB565   0x01
#define MODE_ARGB4444 0x02
#define MODE_YUV422   0x03
#define MODE_BUMPMAP  0x04
#define MODE_RGB555   0x05
#define MODE_ARGB8888 0x06

#define MODE_TWIDDLE            0x0100
#define MODE_TWIDDLE_MIPMAP     0x0200
#define MODE_COMPRESSED         0x0300
#define MODE_COMPRESSED_MIPMAP  0x0400
#define MODE_CLUT4              0x0500
#define MODE_CLUT4_MIPMAP       0x0600
#define MODE_CLUT8              0x0700
#define MODE_CLUT8_MIPMAP       0x0800
#define MODE_RECTANGLE          0x0900
#define MODE_STRIDE             0x0b00
#define MODE_TWIDDLED_RECTANGLE 0x0d00

typedef struct rgba
{
  unsigned char r, g, b, a;
} rgba_group;


static int twiddletab[1024];
static int twiddleinited=0;

static void error( char * s )
{
  fprintf( stderr, "%s", s );
}

static void init_twiddletab()
{
  int x;
  for(x=0; x<1024; x++)
    twiddletab[x] = (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)|
      ((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9);
  twiddleinited=1;
}

static void pvr_encode_rect(int attr, rgba_group *src, unsigned char *dst,
			    unsigned int h, unsigned int w)
{
  int cnt = h * w;
  switch(attr&0xff) 
  {
   case MODE_RGB565:
     while(cnt--) {
       unsigned int p =
	 ((src->r&0xf8)<<8)|((src->g&0xfc)<<3)|((src->b&0xf8)>>3);
       *dst++=p&0xff;
       *dst++=(p&0xff00)>>8;
       src++;
     }
     break;
  }
}

static void pvr_encode_twiddled(int attr, rgba_group *src, unsigned char *d,
				unsigned int sz)
{
  unsigned int x, y;
  switch(attr&0xff) {
   case MODE_RGB565:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 unsigned char *dst = d+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 unsigned int p =
	   ((src->r&0xf8)<<8)|((src->g&0xfc)<<3)|((src->b&0xf8)>>3);
	 *dst++=p&0xff;
	 *dst=(p&0xff00)>>8;
	 src++;
       }
     }
     break;
  }
}

static void pvr_encode_alpha_rect(int attr, rgba_group *src, 
				  unsigned char *dst,
				  unsigned int h, unsigned int w)
{
  int cnt = h * w;
  switch(attr&0xff) {
   case MODE_ARGB1555:
     while(cnt--) {
       unsigned int p =
	 ((src->r&0xf8)<<7)|((src->g&0xf8)<<2)|((src->b&0xf8)>>3);
       if(src->a&0x80)
	 p |= 0x8000;
       *dst++=p&0xff;
       *dst++=(p&0xff00)>>8;
       src++;
     }
     break;
   case MODE_ARGB4444:
     while(cnt--) {
       unsigned int p =
	 ((src->a&0xf0)<<8)|
	 ((src->r&0xf0)<<4)|(src->g&0xf0)|((src->b&0xf0)>>4);
       *dst++=p&0xff;
       *dst++=(p&0xff00)>>8;
       src++;
     }
     break;
  }
}

static void pvr_encode_alpha_twiddled(int attr, rgba_group *src,
                                      unsigned char *d,
				      unsigned int sz)
{
  unsigned int x, y;
  switch(attr&0xff) {
   case MODE_ARGB1555:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 unsigned char *dst = d+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 unsigned int p =
	   ((src->r&0xf8)<<7)|((src->g&0xf8)<<2)|((src->b&0xf8)>>3);
	 if(src->a&0x80)
	   p |= 0x8000;
	 *dst++=p&0xff;
	 *dst++=(p&0xff00)>>8;
	 src++;
       }
     }
     break;
   case MODE_ARGB4444:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 unsigned char *dst = d+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 unsigned int p =
	   ((src->a&0xf0)<<8)|
	   ((src->r&0xf0)<<4)|(src->g&0xf0)|((src->b&0xf0)>>4);
	 *dst++=p&0xff;
	 *dst++=(p&0xff00)>>8;
	 src++;
       }
     }
     break;
  }
}


static void pvr_decode_rect(int attr, unsigned char *src, rgba_group *dst,
			    int stride, unsigned int h, unsigned int w)
{
  int cnt = h * w;
  switch(attr&0xff) {
   case MODE_ARGB1555:
   case MODE_RGB555:
     while(cnt--) {
       unsigned int p = src[0]|(src[1]<<8);
       dst->r = ((p&0x7c00)>>7)|((p&0x7000)>>12);
       dst->g = ((p&0x03e0)>>2)|((p&0x0380)>>7);
       dst->b = ((p&0x001f)<<3)|((p&0x001c)>>2);
       dst->a = 255;
       src+=2;
       dst++;
     }
     break;
   case MODE_RGB565:
     while(cnt--) {
       unsigned int p = src[0]|(src[1]<<8);
       dst->r = ((p&0xf800)>>8)|((p&0xe000)>>13);
       dst->g = ((p&0x07e0)>>3)|((p&0x0600)>>9);
       dst->b = ((p&0x001f)<<3)|((p&0x001c)>>2);
       dst->a = 255;
       src+=2;
       dst++;
     }
     break;
   case MODE_ARGB4444:
     while(cnt--) {
       unsigned int p = src[0]|(src[1]<<8);
       dst->r = ((p&0x0f00)>>4)|((p&0x0f00)>>8);
       dst->g = (p&0x00f0)|((p&0x00f0)>>4);
       dst->b = ((p&0x000f)<<4)|(p&0x000f);
       dst->a = 255;
       src+=2;
       dst++;
     }
     break;
  }
}

static void pvr_decode_twiddled(int attr, unsigned char *s, rgba_group *dst,
				int stride, unsigned int sz)
{
  unsigned int x, y;
  unsigned char *src;
  switch(attr&0xff) {
   case MODE_ARGB1555:
   case MODE_RGB555:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 unsigned int p;
	 src = s+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 p = src[0]|(src[1]<<8);
	 dst->r = ((p&0x7c00)>>7)|((p&0x7000)>>12);
	 dst->g = ((p&0x03e0)>>2)|((p&0x0380)>>7);
	 dst->b = ((p&0x001f)<<3)|((p&0x001c)>>2);
         dst->a = 255;
	 dst++;
       }
       dst += stride;
     }
     break;
   case MODE_RGB565:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 unsigned int p;
	 src = s+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 p = src[0]|(src[1]<<8);
	 dst->r = ((p&0xf800)>>8)|((p&0xe000)>>13);
	 dst->g = ((p&0x07e0)>>3)|((p&0x0600)>>9);
	 dst->b = ((p&0x001f)<<3)|((p&0x001c)>>2);
         dst->a = 255;
	 dst++;
       }
       dst += stride;
     }
     break;
   case MODE_ARGB4444:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 unsigned int p;
	 src = s+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 p = src[0]|(src[1]<<8);
	 dst->r = ((p&0x0f00)>>4)|((p&0x0f00)>>8);
	 dst->g = (p&0x00f0)|((p&0x00f0)>>4);
	 dst->b = ((p&0x000f)<<4)|(p&0x000f);
         dst->a = 255;
	 dst++;
       }
       dst += stride;
     }
     break;
  }
}

static void pvr_decode_alpha_rect(int attr, unsigned char *src,
				  rgba_group *dst, int stride,
				  unsigned int h, unsigned int w)
{
  int cnt = h * w;
  switch(attr&0xff) {
   case MODE_ARGB1555:
     while(cnt--) {
       if(src[1]&0x80)
         dst->a = ~0;
       else
         dst->a = 0;
       src+=2;
       dst++;
     }
     break;
   case MODE_ARGB4444:
     while(cnt--) {
       int a = src[1]&0xf0;
       a |= a>>4;
       dst->a = a;
       src+=2;
       dst++;
     }
     break;
  }
}

static void pvr_decode_alpha_twiddled(int attr, unsigned char *s,
				      rgba_group *dst, int stride,
				      unsigned int sz)
{
  unsigned int x, y;
  unsigned char *src;
  switch(attr&0xff) {
   case MODE_ARGB1555:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 if(s[(((twiddletab[x]<<1)|twiddletab[y])<<1)+1]&0x80)
	   dst->a = ~0;
	 else
	   dst->a = 0;
	 dst++;
       }
       dst += stride;
     }
     break;
   case MODE_ARGB4444:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 int a = s[(((twiddletab[x]<<1)|twiddletab[y])<<1)+1]&0xf0;
	 a |= a>>4;
	 dst->a = a;
	 dst++;
       }
       dst += stride;
     }
     break;
  }
}

struct info
{
  int mode;
  int has_gbix, realalpha;
} info;


/* Declare some local functions.
 */
static void   query          (void);
static void   run            (gchar   *name,
			      gint     nparams,
			      GParam  *param,
			      gint    *nreturn_vals,
			      GParam **return_vals);

static gint32 load_image     (gchar  *filename);
static gint   save_image     (gchar  *filename,
			      gint32  image_ID,
			      gint32  drawable_ID);

static gint   save_dialog    (int id);
static void   ok_callback    (GtkWidget *widget, 
			      gpointer   data);

GPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

MAIN ()

static void
query (void)
{
  static GParamDef load_args[] =
  {
    { PARAM_INT32,  "run_mode",       "Interactive, non-interactive" },
    { PARAM_STRING, "filename",       "The name of the file to load" },
    { PARAM_STRING, "raw_filename",   "The name of the file to load" }
  };
  static GParamDef load_return_vals[] =
  {
    { PARAM_IMAGE,  "image",          "Output image" }
  };
  static gint nload_args = sizeof (load_args) / sizeof (load_args[0]);
  static gint nload_return_vals = (sizeof (load_return_vals) /
				   sizeof (load_return_vals[0]));

  static GParamDef save_args[] =
  {
    { PARAM_INT32,    "run_mode",     "Interactive, non-interactive" },
    { PARAM_IMAGE,    "image",        "Input image" },
    { PARAM_DRAWABLE, "drawable",     "Drawable to save" },
    { PARAM_STRING,   "filename",     "The name of the file to save the image in" },
    { PARAM_STRING,   "raw_filename", "The name of the file to save the image in" },
    { PARAM_INT32,    "mode",          "Output image format" },
  };
  static gint nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_pvr_load",
                          "loads files of the .pvr file format (Dream Cast)",
                          "FIXME: write help",
                          "Per Hedbor",
                          "Per Hedbor",
                          "2000",
                          "<Load>/PVR",
                          NULL,
                          PROC_PLUG_IN,
                          nload_args, nload_return_vals,
                          load_args, load_return_vals);

  gimp_install_procedure ("file_pvr_save",
                          "saves files in the .pvr file format",
                          "Docs",
                          "Per Hedbor",
                          "Per Hedbor",
                          "2000",
                          "<Save>/PVR",
                          "RGB*",
                          PROC_PLUG_IN,
                          nsave_args, 0,
                          save_args, NULL);

  gimp_register_magic_load_handler ("file_pvr_load",
				    "pvr",
				    "",
				    "0,string,PVRT");

  gimp_register_save_handler       ("file_pvr_save",
				    "pvr",
				    "");
}

static void run (gchar   *name,
                 gint     nparams,
                 GParam  *param,
                 gint    *nreturn_vals,
                 GParam **return_vals)
{
  static GParam values[2];
  GRunModeType  run_mode;
  GStatusType   status = STATUS_SUCCESS;
  gint32        image_ID;
  gint32        drawable_ID;
  GimpExportReturnType export = EXPORT_CANCEL;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;
  values[0].type          = PARAM_STATUS;
  values[0].data.d_status = STATUS_EXECUTION_ERROR;

  if (strcmp (name, "file_pvr_load") == 0) 
  {
    image_ID = load_image (param[1].data.d_string);
      
    if (image_ID != -1) 
    {
      *nreturn_vals = 2;
      values[1].type         = PARAM_IMAGE;
      values[1].data.d_image = image_ID;
      gimp_set_data ("file_pvr_save", &info, sizeof(info));
    }
    else
    {
      status = STATUS_EXECUTION_ERROR;
    }
  }
  else if (strcmp (name, "file_pvr_save") == 0) 
  {
    image_ID    = param[1].data.d_int32;
    drawable_ID = param[2].data.d_int32;

    /*  eventually export the image */ 
    switch (run_mode)
    {
     case RUN_INTERACTIVE:
     case RUN_WITH_LAST_VALS:
       gimp_ui_init ("pvr", FALSE);
       export = gimp_export_image (&image_ID, &drawable_ID, "PVR", 
                                   CAN_HANDLE_RGB|CAN_HANDLE_ALPHA);
       if (export == EXPORT_CANCEL)
       {
         values[0].data.d_status = STATUS_CANCEL;
         return;
       }
       break;
     default:
       break;
    }

    switch (run_mode) 
    {
     case RUN_INTERACTIVE:
       /*  Possibly retrieve data  */
       gimp_get_data ("file_pvr_save", &info);
       if (! save_dialog ( drawable_ID ))
         status = STATUS_CANCEL;
       break;

     case RUN_NONINTERACTIVE:  /* FIXME - need a real RUN_NONINTERACTIVE */
       if (nparams != 6)
         status = STATUS_CALLING_ERROR;
       else
         info.mode = (param[5].data.d_int32) & 0xff;
       break;

     case RUN_WITH_LAST_VALS:
       gimp_get_data ("file_pvr_save", &info);
       break;
    }

    if (status == STATUS_SUCCESS)
      if (save_image (param[3].data.d_string, image_ID, drawable_ID)) 
        gimp_set_data ("file_pvr_save", &info, sizeof(info));
      else
        status = STATUS_EXECUTION_ERROR;

    if (export == EXPORT_EXPORT)
      gimp_image_delete (image_ID);
  }
  else
    status = STATUS_CALLING_ERROR;

  values[0].data.d_status = status;
}


int file_size( int fd )
{
  struct stat s;
  fstat( fd, &s );
  return s.st_size;
}

static gint32 load_image( gchar *filename )
{
  unsigned char *s;
  ptrdiff_t len;
  int attr;
  GPixelRgn pixel_rgn;
  unsigned int h, w, x;
  int image_ID, layer_ID;
  int fd = open(filename, O_RDONLY | _O_BINARY);
  gchar *temp = g_strdup_printf ("Loading %s:", filename);

  gimp_progress_init (temp);
  g_free (temp);
  
  if (fd == -1) 
    return -1;

  s = malloc( (len = file_size( fd )) );
  read( fd, s, len );
  if( !s )
    return -1;

  if(len >= 12 && !strncmp(s, "GBIX", 4)) 
  {
    int l = s[4]|(s[5]<<8)|(s[6]<<16)|(s[7]<<24);
    info.has_gbix = 1;
    if(l>=4 && l<=len-8) {
      len -= l+8;
      s += l+8;
    }
  } else
    info.has_gbix = 0;

  if((len < 16) || strncmp(s, "PVRT", 4))
  {
    error("not a PVR texture\n");
    fprintf( stderr, "%d, %d, %d, %d\n", s[0],s[1],s[2],s[3] );
    return -1;
  }
  else 
  {
    int l = s[4]|(s[5]<<8)|(s[6]<<16)|(s[7]<<24);
    if(l+8>len)
    {
      error("file is truncated\n");
      return -1;
    }
    else if(l<8)
    {
      error("invalid PVRT chunk length\n");
      return -1;
    }
    len = l+8;
  }
  gimp_progress_update( 0.5 );

  attr = s[8]|(s[9]<<8)|(s[10]<<16)|(s[11]<<24);
  info.mode = attr & 0xff;
  w = s[12]|(s[13]<<8);
  h = s[14]|(s[15]<<8);

  s += 16;
  len -= 16;

  image_ID = gimp_image_new( w, h, RGB );
  gimp_image_set_filename( image_ID, filename );
  layer_ID = gimp_layer_new( image_ID, "Background", w, h, 
                             RGBA_IMAGE, 100, NORMAL_MODE );
  gimp_image_add_layer (image_ID, layer_ID, 0);

  {
    rgba_group *img;
    GDrawable *drawable = gimp_drawable_get (layer_ID);
    int twiddle=0, hasalpha=0, bpp=0;
    struct object *o;
    int mipmap=0;


    img = malloc( sizeof( rgba_group ) * w * h );
    gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, drawable->width,
                         drawable->height, TRUE, FALSE);


    switch(attr&0xff00) 
    {
     case MODE_TWIDDLE_MIPMAP:
       mipmap = 1;
     case MODE_TWIDDLE:
       twiddle = 1;
       if(w != h || w<8 || w>1024 || (w&(w-1)))
       {
         error("invalid size for twiddle texture\n");
         return -1;
       }
     case MODE_RECTANGLE:
     case MODE_STRIDE:
       break;
     case MODE_TWIDDLED_RECTANGLE:
       twiddle = 1;
       if((w<h && (w<8 || w>1024 || (w&(w-1)) || h%w)) ||
          (h>=w && (h<8 || h>1024 || (h&(h-1)) || w%h)))
       {
         error("invalid size for twiddle rectangle texture\n");
         return -1;
       }
       break;
     case MODE_COMPRESSED:
     case MODE_COMPRESSED_MIPMAP:
       error("compressed PVRs not supported\n");
       return -1;
     case MODE_CLUT4:
     case MODE_CLUT4_MIPMAP:
     case MODE_CLUT8:
     case MODE_CLUT8_MIPMAP:
       error("palette PVRs not supported\n");
       return -1;
     default:
       error("unknown PVR format\n");
       return -1;
    }

    switch(attr&0xff) {
     case MODE_ARGB1555:
     case MODE_ARGB4444:
       hasalpha=1;
     case MODE_RGB565:
     case MODE_RGB555:
       bpp=2; break;
     case MODE_YUV422:
       error("YUV mode not supported\n");
       return -1;
     case MODE_ARGB8888:
       error("ARGB8888 mode not supported\n");
       return -1;
     case MODE_BUMPMAP:
       error("bumpmap mode not supported\n");
       return -1;
     default:
       error("unknown PVR color mode\n");
       return -1;
    }

    if(mipmap) /* Just skip everything except the largest version */
      for(x=w; x>>=1;)
        mipmap += x*x;

    if(len < (int)(bpp*(h*w+mipmap)))
    {
      error("short PVRT chunk\n");
      return -1;
    }

    s += bpp*mipmap;

    if(twiddle && !twiddleinited)
      init_twiddletab();
     
    if(twiddle)
      if(h<w)
        for(x=0; x<w; x+=h)
        {
          gimp_progress_update( ((double)x/w)/2.0 + 0.5 );
          pvr_decode_twiddled(attr, s+bpp*h*x, img+x, w-h, h);
        }
      else
        for(x=0; x<h; x+=w)
        {
          gimp_progress_update( ((double)x/h)/2.0 + 0.5 );
          pvr_decode_twiddled(attr, s+bpp*w*x, img+w*x, 0, w);
        }
    else
      pvr_decode_rect(attr, s, img, 0, h, w);

    if(hasalpha) 
    {
      if(twiddle)
        if(h<w)
          for(x=0; x<w; x+=h)
          {
            gimp_progress_update( ((double)x/w)/2.0 + 0.5 );
            pvr_decode_alpha_twiddled(attr, s+bpp*h*x, img+x, w-h, h);
          }
        else
          for(x=0; x<h; x+=w)
          {
            gimp_progress_update( ((double)x/h)/2.0 + 0.5 );
            pvr_decode_alpha_twiddled(attr, s+bpp*w*x, img+w*x, 0, w);
          }
      else
        pvr_decode_alpha_rect(attr, s, img, 0, h, w);
    }

    for( x = 0; x<h; x++ )
    {
      gimp_pixel_rgn_set_row( &pixel_rgn, (char *)(img + (w*x)), 0, x, w );
      gimp_progress_update( ((double) x / (double) h) );
    }
    gimp_drawable_flush (drawable);
  }
  return image_ID;
}


static gint  save_image (char   *filename, 
                         gint32  image_ID, 
                         gint32  drawable_ID) 
{
  int fd;
  int y;
  int gbix=0, sz, attr=0;
  int has_gbix=0, twiddle=0, alpha;
  rgba_group *buffer;
  GDrawable *drawable;
  gint line;
  GPixelRgn pixel_rgn;
  char *temp;
  
  if ((gimp_drawable_type(drawable_ID) != RGB_IMAGE) &&
      (gimp_drawable_type(drawable_ID) != RGBA_IMAGE))
    return FALSE;
  
  temp = g_strdup_printf ("Saving %s:", filename);
  gimp_progress_init (temp);
  g_free (temp);
  
  drawable = gimp_drawable_get(drawable_ID);
  gimp_pixel_rgn_init(&pixel_rgn, drawable, 0, 0, drawable->width,
		      drawable->height, FALSE, FALSE);
  
  fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY | _O_BINARY, 0644);
  
  if (fd == -1) 
  {
    g_message( "Unable to open %s", filename);
    return FALSE;
  }

  buffer = malloc(drawable->width*drawable->height*sizeof(rgba_group));

  if( gimp_drawable_type(drawable_ID) == RGB_IMAGE )
  {
    unsigned char *tmp;
    rgba_group *bp = buffer;
    alpha = 0;
    tmp = alloca( drawable->width * 3 );
    for( y = 0; y<drawable->height; y++ )
    {
      int x;
      gimp_pixel_rgn_get_row( &pixel_rgn, tmp, 0, y, drawable->width );
      gimp_progress_update( ((double)y / (double)drawable->height ) );
      for( x = 0; x<drawable->width; x++ )
      {
        bp->r = tmp[x*3+0];
        bp->g = tmp[x*3+1];
        bp->b = tmp[x*3+2];
        (bp++)->a = 255;
      }
    }
  }
  else if( gimp_drawable_type(drawable_ID) == RGBA_IMAGE )
  {
    alpha = 1;
    for( y = 0; y<drawable->height; y++ )
    {
      gimp_progress_update( ((double)y / (double)drawable->height ) );
      gimp_pixel_rgn_get_row( &pixel_rgn, (char *)(buffer+y*drawable->width), 
                              0, y, drawable->width );
    }
  }


  {
    char *dst, *dst0;
    int len;
    dst0 = dst = malloc( (len = (8+(sz=8+2*drawable->width*drawable->height)
                                 +(info.has_gbix? 12:0)) ));
    

    if( alpha )
      if( info.realalpha )
        info.mode = MODE_ARGB4444;
      else
        info.mode = MODE_ARGB1555;

    attr = info.mode;

    if(drawable->width == drawable->height 
       && drawable->width>=8 
       && drawable->height<=1024 &&
       !(drawable->width&(drawable->width-1)))
    {
      attr |= MODE_TWIDDLE;
      twiddle = 1;
    } else
      attr |= MODE_RECTANGLE;
    
    if(info.has_gbix) 
    {
      *dst++ = 'G';
      *dst++ = 'B';
      *dst++ = 'I';
      *dst++ = 'X';
      *dst++ = 4;
      *dst++ = 0;
      *dst++ = 0;
      *dst++ = 0;
      *dst++ = (gbix&0x000000ff);
      *dst++ = (gbix&0x0000ff00)>>8;
      *dst++ = (gbix&0x00ff0000)>>16;
      *dst++ = (gbix&0xff000000)>>24;
    }
    *dst++ = 'P';
    *dst++ = 'V';
    *dst++ = 'R';
    *dst++ = 'T';
    *dst++ = (sz&0x000000ff);
    *dst++ = (sz&0x0000ff00)>>8;
    *dst++ = (sz&0x00ff0000)>>16;
    *dst++ = (sz&0xff000000)>>24;
    *dst++ = (attr&0x000000ff);
    *dst++ = (attr&0x0000ff00)>>8;
    *dst++ = (attr&0x00ff0000)>>16;
    *dst++ = (attr&0xff000000)>>24;
    *dst++ = (drawable->width&0x00ff);
    *dst++ = (drawable->width&0xff00)>>8;
    *dst++ = (drawable->height&0x00ff);
    *dst++ = (drawable->height&0xff00)>>8;

    if(twiddle && !twiddleinited)
      init_twiddletab();

    if(alpha)
      if(twiddle)
        pvr_encode_alpha_twiddled(attr, buffer, dst, drawable->width);
    else
      pvr_encode_alpha_rect(attr,buffer,dst,drawable->height,drawable->width );
    else
      if(twiddle)
        pvr_encode_twiddled(attr, buffer, dst, drawable->width);
      else
        pvr_encode_rect(attr, buffer, dst, drawable->height, drawable->width);

    write( fd, dst0, len );
  }
  close(fd);
  return TRUE;
}


static void toggle_4bit (GtkWidget *widget, 
                         gpointer   nil)
{
  if( info.mode != MODE_ARGB1555 )
    info.mode = MODE_ARGB1555;
  else
    info.mode = MODE_ARGB4444;
}

static gint save_dialog( int drwable_ID )
{
  int alpha = 0;
  GtkWidget *dlg;
  GtkWidget *table;
  GtkObject *adj;
  GtkWidget *toggle;

  dlg = gimp_dialog_new ("Save as PVR", "pvr",
			 gimp_standard_help_func, "filters/pvr.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 "OK", ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,
			 "Cancel", gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,

			 NULL);

  gtk_signal_connect (GTK_OBJECT(dlg), "destroy",
		      GTK_SIGNAL_FUNC (gtk_main_quit),
		      NULL);


  if( gimp_drawable_type(drwable_ID) == RGBA_IMAGE )
    alpha = 1;

  /* The main table */
  table = gtk_table_new (2, 2, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);

  toggle = gtk_check_button_new_with_label ( "Include GBIX chunk" );
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 2, 0, 1,
		    GTK_FILL, 0, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &info.has_gbix);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), info.has_gbix);
  gtk_widget_show (toggle);

  if( alpha )
  {
    toggle=gtk_check_button_new_with_label ( "4bit alpha (instead of 1bit)" );
    gtk_table_attach (GTK_TABLE (table), toggle, 0, 2, 1, 2,
                      GTK_FILL, 0, 0, 0);
    gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
                        GTK_SIGNAL_FUNC (gimp_toggle_button_update),
                        &info.realalpha);
    switch(info.mode)
    {
     case MODE_ARGB4444:
       info.realalpha = 1;
       gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON (toggle), 1);
       break;
     default:
       info.mode = MODE_ARGB1555;
     case MODE_ARGB1555:
       info.realalpha = 0;
       gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON (toggle), 0);
       break;
    }
    gtk_widget_show (toggle);
  } else
    info.mode = MODE_RGB565;

  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), table, TRUE, TRUE, 0);
  gtk_widget_show (table);
  
  gtk_widget_show (dlg);
  
  gtk_main ();
  gdk_flush ();
  
  return run_flag;
}

static void  ok_callback(GtkWidget *widget, 
                         gpointer   data)
{
  run_flag = 1;
  gtk_widget_destroy (GTK_WIDGET (data));
}
