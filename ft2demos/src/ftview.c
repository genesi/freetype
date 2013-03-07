/****************************************************************************/
/*                                                                          */
/*  The FreeType project -- a free and portable quality TrueType renderer.  */
/*                                                                          */
/*  Copyright 1996-2000, 2003-2007, 2009-2012 by                            */
/*  D. Turner, R.Wilhelm, and W. Lemberg                                    */
/*                                                                          */
/*                                                                          */
/*  FTView - a simple font viewer.                                          */
/*                                                                          */
/*  This is a new version using the MiGS graphics subsystem for             */
/*  blitting and display.                                                   */
/*                                                                          */
/*  Press F1 when running this program to have a list of key-bindings       */
/*                                                                          */
/****************************************************************************/


#include "ftcommon.h"
#include "common.h"
#include <math.h>
#include <stdio.h>

  /* the following header shouldn't be used in normal programs */
#include FT_INTERNAL_DEBUG_H
#include FT_STROKER_H
#include FT_SYNTHESIS_H
#include FT_LCD_FILTER_H

#define MAXPTSIZE      500                 /* dtp */
#define HEADER_HEIGHT  8

#ifdef CEIL
#undef CEIL
#endif
#define CEIL( x )  ( ( (x) + 63 ) >> 6 )

#define INIT_SIZE( size, start_x, start_y, step_y, x, y )               \
          do {                                                          \
            start_x = 4;                                                \
            start_y = CEIL( size->metrics.height ) + 3 * HEADER_HEIGHT; \
            step_y  = CEIL( size->metrics.height ) + 4;                 \
                                                                        \
            x = start_x;                                                \
            y = start_y;                                                \
          } while ( 0 )

#define X_TOO_LONG( x, size, display )                   \
          ( (x) + ( (size)->metrics.max_advance >> 6 ) > \
            (display)->bitmap->width )
#define Y_TOO_LONG( y, size, display )       \
          ( (y) >= (display)->bitmap->rows )

#ifdef _WIN32
#define snprintf  _snprintf
#endif


  enum
  {
    RENDER_MODE_ALL = 0,
    RENDER_MODE_EMBOLDEN,
    RENDER_MODE_SLANTED,
    RENDER_MODE_STROKE,
    RENDER_MODE_TEXT,
    RENDER_MODE_WATERFALL,
    N_RENDER_MODES
  };

  static struct  status_
  {
    int            render_mode;
    FT_Encoding    encoding;
    int            res;
    int            ptsize;            /* current point size, 26.6 format */
    int            lcd_mode;
    double         gamma;
    double         xbold_factor;
    double         ybold_factor;
    double         radius;
    double         slant;

    int            debug;
    int            trace_level;
    int            font_index;
    int            dump_cache_stats;  /* do we need to dump cache statistics? */
    int            Num;               /* current first index */
    char*          header;
    char           header_buffer[256];
    int            Fail;
    int            preload;

    int            use_custom_lcd_filter;
    unsigned char  filter_weights[5];
    int            fw_index;

  } status = { RENDER_MODE_ALL, FT_ENCODING_NONE, 72, 48, -1,
               1.0, 0.04, 0.04, 0.02, 0.22,
               0, 0, 0, 0, 0, NULL, { 0 }, 0, 0,
               0, { 0x10, 0x40, 0x70, 0x40, 0x10 }, 2 };


  static FTDemo_Display*  display;
  static FTDemo_Handle*   handle;


  static const unsigned char*  Text = (unsigned char*)
    "The quick brown fox jumps over the lazy dog 0123456789"
    " \342\352\356\373\364\344\353\357\366\374\377\340\371\351\350\347"
    " &#~\"\'(-`_^@)=+\260 ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    " $\243^\250*\265\371%!\247:/;.,?<>";



  static void
  Fatal( const char*  message )
  {
    FTDemo_Display_Done( display );
    FTDemo_Done( handle );
    PanicZ( message );
  }


  static FT_Error
  Render_Stroke( int  num_indices,
                 int  first_index )
  {
    int           start_x, start_y, step_y, x, y;
    int           i;
    FT_Size       size;
    FT_Face       face;
    FT_GlyphSlot  slot;

    FT_Fixed  radius;


    error = FTDemo_Get_Size( handle, &size );

    if ( error )
    {
      /* probably a non-existent bitmap font size */
      return error;
    }

    INIT_SIZE( size, start_x, start_y, step_y, x, y );
    face = size->face;
    slot = face->glyph;

    radius = status.radius * ( status.ptsize * status.res / 72 );

    FT_Stroker_Set( handle->stroker, radius,
                    FT_STROKER_LINECAP_ROUND,
                    FT_STROKER_LINEJOIN_ROUND,
                    0 );

    for ( i = first_index; i < num_indices; i++ )
    {
      int  gindex;


      if ( handle->encoding == FT_ENCODING_NONE )
        gindex = i;
      else
        gindex = FTDemo_Get_Index( handle, i );

      error = FT_Load_Glyph( face, gindex,
                             handle->load_flags | FT_LOAD_NO_BITMAP );

      if ( !error && slot->format == FT_GLYPH_FORMAT_OUTLINE )
      {
        FT_Glyph  glyph;


        error = FT_Get_Glyph( slot, &glyph );
        if ( error )
          goto Next;

        error = FT_Glyph_Stroke( &glyph, handle->stroker, 1 );
        if ( error )
        {
          FT_Done_Glyph( glyph );
          goto Next;
        }

        error = FTDemo_Draw_Glyph( handle, display, glyph, &x, &y );
        if ( !error )
          FT_Done_Glyph( glyph );

        if ( error )
          goto Next;
        else if ( X_TOO_LONG( x, size, display ) )
        {
          x  = start_x;
          y += step_y;

          if ( Y_TOO_LONG( y, size, display ) )
            break;
        }
      }
      else
    Next:
        status.Fail++;
    }

    return error;
  }


  static FT_Error
  Render_Slanted( int  num_indices,
                  int  first_index )
  {
    int           start_x, start_y, step_y, x, y;
    int           i;
    FT_Size       size;
    FT_Face       face;
    FT_GlyphSlot  slot;

    FT_Matrix  shear;


    error = FTDemo_Get_Size( handle, &size );

    if ( error )
    {
      /* probably a non-existent bitmap font size */
      return error;
    }

    INIT_SIZE( size, start_x, start_y, step_y, x, y );
    face = size->face;
    slot = face->glyph;

    /***************************************************************/
    /*                                                             */
    /*  2*2 affine transformation matrix, 16.16 fixed float format */
    /*                                                             */
    /*  Shear matrix:                                              */
    /*                                                             */
    /*         | x' |     | 1  k |   | x |          x' = x + ky    */
    /*         |    |  =  |      | * |   |   <==>                  */
    /*         | y' |     | 0  1 |   | y |          y' = y         */
    /*                                                             */
    /*        outline'     shear    outline                        */
    /*                                                             */
    /***************************************************************/

    shear.xx = 1 << 16;
    shear.xy = (FT_Fixed)( status.slant * ( 1 << 16 ) );
    shear.yx = 0;
    shear.yy = 1 << 16;

    for ( i = first_index; i < num_indices; i++ )
    {
      int  gindex;


      if ( handle->encoding == FT_ENCODING_NONE )
        gindex = i;
      else
        gindex = FTDemo_Get_Index( handle, i );

      error = FT_Load_Glyph( face, gindex, handle->load_flags );
      if ( !error )
      {
        FT_Outline_Transform( &slot->outline, &shear );

        error = FTDemo_Draw_Slot( handle, display, slot, &x, &y );

        if ( error )
          goto Next;
        else if ( X_TOO_LONG( x, size, display ) )
        {
          x  = start_x;
          y += step_y;

          if ( Y_TOO_LONG( y, size, display ) )
            break;
        }
      }
      else
    Next:
        status.Fail++;
    }

    return error;
  }


  static FT_Error
  Render_Embolden( int  num_indices,
                   int  first_index )
  {
    int           start_x, start_y, step_y, x, y;
    int           i;
    FT_Size       size;
    FT_Face       face;
    FT_GlyphSlot  slot;

    FT_Pos  xstr, ystr;


    error = FTDemo_Get_Size( handle, &size );

    if ( error )
    {
      /* probably a non-existent bitmap font size */
      return error;
    }

    INIT_SIZE( size, start_x, start_y, step_y, x, y );
    face = size->face;
    slot = face->glyph;

    ystr = status.ptsize * status.res / 72;
    xstr = status.xbold_factor * ystr;
    ystr = status.ybold_factor * ystr;

    for ( i = first_index; i < num_indices; i++ )
    {
      int  gindex;


      if ( handle->encoding == FT_ENCODING_NONE )
        gindex = i;
      else
        gindex = FTDemo_Get_Index( handle, i );

      error = FT_Load_Glyph( face, gindex, handle->load_flags );
      if ( !error )
      {
        /* this is essentially the code of function */
        /* `FT_GlyphSlot_Embolden'                  */

        if ( slot->format == FT_GLYPH_FORMAT_OUTLINE )
        {
          error = FT_Outline_EmboldenXY( &slot->outline, xstr, ystr );
          /* ignore error */
        }
        else if ( slot->format == FT_GLYPH_FORMAT_BITMAP )
        {
          /* round to full pixels */
          xstr &= ~63;
          ystr &= ~63;

          error = FT_GlyphSlot_Own_Bitmap( slot );
          if ( error )
            goto Next;

          error = FT_Bitmap_Embolden( slot->library, &slot->bitmap,
                                      xstr, ystr );
          if ( error )
            goto Next;
        } else
          goto Next;

        if ( slot->advance.x )
          slot->advance.x += xstr;

        if ( slot->advance.y )
          slot->advance.y += ystr;

        slot->metrics.width        += xstr;
        slot->metrics.height       += ystr;
        slot->metrics.horiAdvance  += xstr;
        slot->metrics.vertAdvance  += ystr;

        if ( slot->format == FT_GLYPH_FORMAT_BITMAP )
          slot->bitmap_top += ystr >> 6;

        error = FTDemo_Draw_Slot( handle, display, slot, &x, &y );

        if ( error )
          goto Next;
        else if ( X_TOO_LONG( x, size, display ) )
        {
          x  = start_x;
          y += step_y;

          if ( Y_TOO_LONG( y, size, display ) )
            break;
        }
      }
      else
    Next:
        status.Fail++;
    }

    return error;
  }


  static FT_Error
  Render_All( int  num_indices,
              int  first_index )
  {
    int      start_x, start_y, step_y, x, y;
    int      i;
    FT_Size  size;


    error = FTDemo_Get_Size( handle, &size );

    if ( error )
    {
      /* probably a non-existent bitmap font size */
      return error;
    }

    INIT_SIZE( size, start_x, start_y, step_y, x, y );

    for ( i = first_index; i < num_indices; i++ )
    {
      int  gindex;


      if ( handle->encoding == FT_ENCODING_NONE )
        gindex = i;
      else
        gindex = FTDemo_Get_Index( handle, i );

      error = FTDemo_Draw_Index( handle, display, gindex, &x, &y );
      if ( error )
        status.Fail++;
      else if ( X_TOO_LONG( x, size, display ) )
      {
        x = start_x;
        y += step_y;

        if ( Y_TOO_LONG( y, size, display ) )
          break;
      }
    }

    return FT_Err_Ok;
  }


  static FT_Error
  Render_Text( int  num_indices,
               int  first_index )
  {
    int      start_x, start_y, step_y, x, y;
    FT_Size  size;

    const char*  p;
    const char*  pEnd;


    error = FTDemo_Get_Size( handle, &size );
    if ( error )
    {
      /* probably a non-existent bitmap font size */
      return error;
    }

    INIT_SIZE( size, start_x, start_y, step_y, x, y );

    p    = (const char*)Text;
    pEnd = p + strlen( (const char*)Text );

    while ( first_index-- )
      utf8_next( &p, pEnd );

    while ( num_indices-- )
    {
      FT_UInt  gindex;
      int      ch;


      ch = utf8_next( &p, pEnd );
      if ( ch < 0 )
        break;

      gindex = FTDemo_Get_Index( handle, ch );

      error = FTDemo_Draw_Index( handle, display, gindex, &x, &y );
      if ( error )
        status.Fail++;
      else
      {
        /* Draw_Index adds one pixel space */
        x--;

        if ( X_TOO_LONG( x, size, display ) )
        {
          x  = start_x;
          y += step_y;

          if ( Y_TOO_LONG( y, size, display ) )
            break;
        }
      }
    }

    return FT_Err_Ok;
  }


  static FT_Error
  Render_Waterfall( int  first_size )
  {
    int      start_x, start_y, step_y, x, y;
    int      pt_size, max_size = 100000;
    FT_Size  size;
    FT_Face  face;

    unsigned char         text[256];
    const unsigned char*  p;


    error = FTC_Manager_LookupFace( handle->cache_manager,
                                    handle->scaler.face_id, &face );
    if ( error )
    {
      /* can't access the font file: do not render anything */
      fprintf( stderr, "can't access font file %p\n",
               (void*)handle->scaler.face_id );
      return 0;
    }

    if ( !FT_IS_SCALABLE( face ) )
    {
      int  i;


      max_size = 0;
      for ( i = 0; i < face->num_fixed_sizes; i++ )
        if ( face->available_sizes[i].height >= max_size / 64 )
          max_size = face->available_sizes[i].height * 64;
    }

    start_x = 4;
    start_y = 3 * HEADER_HEIGHT;

    for ( pt_size = first_size; pt_size < max_size; pt_size += 64 )
    {
      FTDemo_Set_Current_Charsize( handle, pt_size, status.res );

      error = FTDemo_Get_Size( handle, &size );
      if ( error )
      {
        /* probably a non-existent bitmap font size */
        continue;
      }

      step_y = ( size->metrics.height >> 6 ) + 1;

      x = start_x;
      y = start_y + ( size->metrics.ascender >> 6 );

      start_y += step_y;

      if ( y >= display->bitmap->rows )
        break;

      sprintf( (char*)text,
               "%g: the quick brown fox jumps over the lazy dog"
               " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", pt_size / 64.0 );

      for ( p = text; *p; p++ )
      {
        FT_UInt  gindex;


        gindex = FTDemo_Get_Index( handle, *p );

        error = FTDemo_Draw_Index( handle, display, gindex, &x, &y );
        if ( error )
          status.Fail++;
        else if ( X_TOO_LONG( x, size, display ) )
          break;
      }
    }

    FTDemo_Set_Current_Charsize( handle, first_size, status.res );

    return FT_Err_Ok;
  }


  /*************************************************************************/
  /*************************************************************************/
  /*****                                                               *****/
  /*****                REST OF THE APPLICATION/PROGRAM                *****/
  /*****                                                               *****/
  /*************************************************************************/
  /*************************************************************************/

  static void
  event_help( void )
  {
    grEvent  dummy_event;


    FTDemo_Display_Clear( display );
    grGotoxy( 0, 0 );
    grSetMargin( 2, 1 );
    grGotobitmap( display->bitmap );

    grWriteln( "FreeType Glyph Viewer - part of the FreeType test suite" );
    grLn();
    grWriteln( "This program is used to display all glyphs from one or" );
    grWriteln( "several font files, with the FreeType library." );
    grLn();
    grWriteln( "Use the following keys:" );
    grLn();
    grWriteln( "  F1, ?       display this help screen" );
    grLn();
    grWriteln( "  a           toggle anti-aliasing" );
    grWriteln( "  b           toggle embedded bitmaps" );
    grWriteln( "  c           toggle between cache modes" );
    grWriteln( "  f           toggle forced auto-hinting" );
    grWriteln( "  h           toggle outline hinting" );
    grWriteln( "  l           toggle low precision rendering" );
    grLn();
    grWriteln( "  K           cycle backwards through LCD modes" );
    grWriteln( "  L           cycle forwards through LCD modes" );
    grWriteln( "  backspace   cycle backwards through rendering modes" );
    grWriteln( "  space       cycle forwards through rendering modes" );
    grWriteln( "  1-6         select rendering mode" );
    grLn();
    grWriteln( "  x, X        adjust horizontal emboldening" );
    grWriteln( "  y, Y        adjust vertical emboldening" );
    grWriteln( "  r, R        adjust stroking radius" );
    grWriteln( "  s, S        adjust slanting" );
    grLn();
    grWriteln( "  F           toggle custom LCD filter mode" );
    grWriteln( "  [, ]        select custom LCD filter weight" );
    grWriteln( "  -, +(=)     adjust selected custom LCD filter weight" );
    grLn();
    grWriteln( "  G           show gamma ramp" );
    grWriteln( "  g, v        adjust gamma value" );
    grLn();
    grWriteln( "  p, n        select previous/next font" );
    grLn();
    grWriteln( "  Up, Down    adjust pointsize by 1 unit" );
    grWriteln( "  PgUp, PgDn  adjust pointsize by 10 units" );
    grLn();
    grWriteln( "  Left, Right adjust index by 1" );
    grWriteln( "  F7, F8      adjust index by 10" );
    grWriteln( "  F9, F10     adjust index by 100" );
    grWriteln( "  F11, F12    adjust index by 1000" );
    grLn();
    grWriteln( "press any key to exit this help screen" );

    grRefreshSurface( display->surface );
    grListenSurface( display->surface, gr_event_key, &dummy_event );
  }


  static void
  event_gamma_grid( void )
  {
    grEvent  dummy_event;
    int      g;
    int      yside  = 11;
    int      xside  = 10;
    int      levels = 17;
    int      gammas = 30;
    int      x_0    = ( display->bitmap->width - levels * xside ) / 2;
    int      y_0    = ( display->bitmap->rows - gammas * ( yside + 1 ) ) / 2;
    int      pitch  = display->bitmap->pitch;


    FTDemo_Display_Clear( display );
    grGotobitmap( display->bitmap );

    if ( pitch < 0 )
      pitch = -pitch;

    memset( display->bitmap->buffer, 100, pitch * display->bitmap->rows );

    grWriteCellString( display->bitmap, 0, 0, "Gamma grid",
                       display->fore_color );

    for ( g = 1; g <= gammas; g++ )
    {
      double  ggamma = 0.1 * g;
      char    temp[6];
      int     y = y_0 + ( yside + 1 ) * ( g - 1 );
      int     nx, ny;

      unsigned char*  line = display->bitmap->buffer +
                             y * display->bitmap->pitch;


      if ( display->bitmap->pitch < 0 )
        line -= display->bitmap->pitch * ( display->bitmap->rows - 1 );

      line += x_0 * 3;

      grSetPixelMargin( x_0 - 32, y + ( yside - 8 ) / 2 );
      grGotoxy( 0, 0 );

      sprintf( temp, "%.1f", ggamma );
      grWrite( temp );

      for ( ny = 0; ny < yside; ny++, line += display->bitmap->pitch )
      {
        unsigned char*  dst = line;


        for ( nx = 0; nx < levels; nx++, dst += 3 * xside )
        {
          double  p   = nx / (double)( levels - 1 );
          int     gm  = (int)( 255.0 * pow( p, ggamma ) );


          memset( dst, gm, xside * 3 );
        }
      }
    }

    grRefreshSurface( display->surface );
    grListenSurface( display->surface, gr_event_key, &dummy_event );
  }


  static void
  event_gamma_change( double  delta )
  {
    status.gamma += delta;

    if ( status.gamma > 3.0 )
      status.gamma = 3.0;
    else if ( status.gamma < 0.0 )
      status.gamma = 0.0;

    grSetGlyphGamma( status.gamma );

    sprintf( status.header_buffer, "gamma changed to %.1f%s",
             status.gamma, status.gamma == 0.0 ? " (sRGB mode)" : "" );

    status.header = status.header_buffer;
  }


  static void
  event_bold_change( double  xdelta,
                     double  ydelta )
  {
    status.xbold_factor += xdelta;
    status.ybold_factor += ydelta;

    if ( status.xbold_factor > 0.1 )
      status.xbold_factor = 0.1;
    else if ( status.xbold_factor < -0.1 )
      status.xbold_factor = -0.1;

    if ( status.ybold_factor > 0.1 )
      status.ybold_factor = 0.1;
    else if ( status.ybold_factor < -0.1 )
      status.ybold_factor = -0.1;

    sprintf( status.header_buffer, "embolding factors changed to %.3f,%.3f",
             status.xbold_factor, status.ybold_factor );

    status.header = status.header_buffer;
  }


  static void
  event_radius_change( double  delta )
  {
    status.radius += delta;

    if ( status.radius > 0.05 )
      status.radius = 0.05;
    else if ( status.radius < 0.0 )
      status.radius = 0.0;

    sprintf( status.header_buffer, "stroking radius changed to %.3f",
             status.radius );

    status.header = status.header_buffer;
  }


  static void
  event_slant_change( double  delta )
  {
    status.slant += delta;

    if ( status.slant > 1.0 )
      status.slant = 1.0;
    else if ( status.slant < -1.0 )
      status.slant = -1.0;

    sprintf( status.header_buffer, "slanting changed to %.3f",
             status.slant );

    status.header = status.header_buffer;
  }


  static void
  event_size_change( int  delta )
  {
    status.ptsize += delta;

    if ( status.ptsize < 64 * 1 )
      status.ptsize = 1 * 64;
    else if ( status.ptsize > MAXPTSIZE * 64 )
      status.ptsize = MAXPTSIZE * 64;

    FTDemo_Set_Current_Charsize( handle, status.ptsize, status.res );
  }


  static void
  event_index_change( int  delta )
  {
    int  num_indices = handle->current_font->num_indices;


    status.Num += delta;

    if ( status.Num < 0 )
      status.Num = 0;
    else if ( status.Num >= num_indices )
      status.Num = num_indices - 1;
  }


  static void
  event_render_mode_change( int  delta )
  {

    if ( delta )
    {
      status.render_mode = ( status.render_mode + delta ) % N_RENDER_MODES;

      if ( status.render_mode < 0 )
        status.render_mode += N_RENDER_MODES;
    }

    switch ( status.render_mode )
    {
    case RENDER_MODE_ALL:
      status.header = (char *)"rendering all glyphs in font";
      break;
    case RENDER_MODE_EMBOLDEN:
      status.header = (char *)"rendering emboldened text";
      break;
    case RENDER_MODE_SLANTED:
      status.header = (char *)"rendering slanted text";
      break;
    case RENDER_MODE_STROKE:
      status.header = (char *)"rendering stroked text";
      break;
    case RENDER_MODE_TEXT:
      status.header = (char *)"rendering test text string";
      break;
    case RENDER_MODE_WATERFALL:
      status.header = (char *)"rendering glyph waterfall";
      break;
    }
  }


  static void
  event_font_change( int  delta )
  {
    int  num_indices;


    if ( status.font_index + delta >= handle->num_fonts ||
         status.font_index + delta < 0                  )
      return;

    status.font_index += delta;

    FTDemo_Set_Current_Font( handle, handle->fonts[status.font_index] );
    FTDemo_Set_Current_Charsize( handle, status.ptsize, status.res );
    FTDemo_Update_Current_Flags( handle );

    num_indices = handle->current_font->num_indices;

    if ( status.Num >= num_indices )
      status.Num = num_indices - 1;
  }


  static int
  Process_Event( grEvent*  event )
  {
    int  ret = 0;


    if ( event->key >= '1' && event->key < '1' + N_RENDER_MODES )
    {
      status.render_mode = event->key - '1';
      event_render_mode_change( 0 );

      return ret;
    }

    switch ( event->key )
    {
    case grKeyEsc:
    case grKEY( 'q' ):
      ret = 1;
      break;

    case grKeyF1:
    case grKEY( '?' ):
      event_help();
      break;

    case grKEY( 'a' ):
      handle->antialias = !handle->antialias;
      status.header     = handle->antialias
                           ? (char *)"anti-aliasing is now on"
                           : (char *)"anti-aliasing is now off";

      FTDemo_Update_Current_Flags( handle );
      break;

    case grKEY( 'b' ):
      handle->use_sbits = !handle->use_sbits;
      status.header     = handle->use_sbits
                           ? (char *)"now using embedded bitmaps (if available)"
                           : (char *)"now ignoring embedded bitmaps";

      FTDemo_Update_Current_Flags( handle );
      break;

    case grKEY( 'c' ):
      handle->use_sbits_cache = !handle->use_sbits_cache;
      status.header           = handle->use_sbits_cache
                                 ? (char *)"now using sbits cache"
                                 : (char *)"now using normal cache";
      break;

    case grKEY( 'f' ):
      handle->autohint = !handle->autohint;
      status.header    = handle->autohint
                          ? (char *)"forced auto-hinting is now on"
                          : (char *)"forced auto-hinting is now off";

      FTDemo_Update_Current_Flags( handle );
      break;

    case grKEY( 'h' ):
      handle->hinted = !handle->hinted;
      status.header  = handle->hinted
                        ? (char *)"glyph hinting is now active"
                        : (char *)"glyph hinting is now ignored";

      FTDemo_Update_Current_Flags( handle );
      break;

    case grKEY( 'l' ):
      handle->low_prec = !handle->low_prec;
      status.header    = handle->low_prec
                          ? (char *)"rendering precision is now forced to low"
                          : (char *)"rendering precision is now normal";

      FTDemo_Update_Current_Flags( handle );
      break;

    case grKEY( 'L' ):
    case grKEY( 'K' ):
      handle->lcd_mode = ( event->key == grKEY( 'L' ) )
                         ? ( ( handle->lcd_mode == ( N_LCD_MODES - 1 ) )
                             ? 0
                             : handle->lcd_mode + 1 )
                         : ( ( handle->lcd_mode == 0 )
                             ? ( N_LCD_MODES - 1 )
                             : handle->lcd_mode - 1 );

      switch ( handle->lcd_mode )
      {
      case LCD_MODE_AA:
        status.header = (char *)"use normal anti-aliased rendering";
        break;
      case LCD_MODE_LIGHT:
        status.header = (char *)"use light anti-aliased rendering";
        break;
      case LCD_MODE_RGB:
        status.header = (char *)"use horizontal LCD-optimized rendering (RGB)";
        break;
      case LCD_MODE_BGR:
        status.header = (char *)"use horizontal LCD-optimized rendering (BGR)";
        break;
      case LCD_MODE_VRGB:
        status.header = (char *)"use vertical LCD-optimized rendering (RGB)";
        break;
      case LCD_MODE_VBGR:
        status.header = (char *)"use vertical LCD-optimized rendering (BGR)";
        break;
      }

      FTDemo_Update_Current_Flags( handle );
      break;

    case grKeySpace:
      event_render_mode_change( 1 );
      break;

    case grKeyBackSpace:
      event_render_mode_change( -1 );
      break;

    case grKEY( 'G' ):
      event_gamma_grid();
      break;

    case grKEY( 's' ):
      event_slant_change( 0.02 );
      break;

    case grKEY( 'S' ):
      event_slant_change( -0.02 );
      break;

    case grKEY( 'r' ):
      event_radius_change( 0.005 );
      break;

    case grKEY( 'R' ):
      event_radius_change( -0.005 );
      break;

    case grKEY( 'x' ):
      event_bold_change( 0.005, 0.0 );
      break;

    case grKEY( 'X' ):
      event_bold_change( -0.005, 0.0 );
      break;

    case grKEY( 'y' ):
      event_bold_change( 0.0, 0.005 );
      break;

    case grKEY( 'Y' ):
      event_bold_change( 0.0, -0.005 );
      break;

    case grKEY( 'g' ):
      event_gamma_change( 0.1 );
      break;

    case grKEY( 'v' ):
      event_gamma_change( -0.1 );
      break;

    case grKEY( 'n' ):
      event_font_change( 1 );
      break;

    case grKEY( 'p' ):
      event_font_change( -1 );
      break;

    case grKeyUp:       event_size_change(   64 ); break;
    case grKeyDown:     event_size_change(  -64 ); break;
    case grKeyPageUp:   event_size_change(  640 ); break;
    case grKeyPageDown: event_size_change( -640 ); break;

    case grKeyLeft:  event_index_change(    -1 ); break;
    case grKeyRight: event_index_change(     1 ); break;
    case grKeyF7:    event_index_change(   -10 ); break;
    case grKeyF8:    event_index_change(    10 ); break;
    case grKeyF9:    event_index_change(  -100 ); break;
    case grKeyF10:   event_index_change(   100 ); break;
    case grKeyF11:   event_index_change( -1000 ); break;
    case grKeyF12:   event_index_change(  1000 ); break;

    case grKEY( 'F' ):
      FTC_Manager_RemoveFaceID( handle->cache_manager,
                                handle->scaler.face_id );

      status.use_custom_lcd_filter = !status.use_custom_lcd_filter;
      if ( status.use_custom_lcd_filter )
        FT_Library_SetLcdFilterWeights( handle->library,
                                        status.filter_weights );
      else
        FT_Library_SetLcdFilterWeights( handle->library,
                                        (unsigned char*)"\x10\x40\x70\x40\x10" );
      status.header = status.use_custom_lcd_filter
                      ? (char *)"using custom LCD filter weights"
                      : (char *)"using default LCD filter";
      break;

    case grKEY( '[' ):
      if ( !status.use_custom_lcd_filter )
        break;

      status.fw_index--;
      if ( status.fw_index < 0 )
        status.fw_index = 4;
      break;

    case grKEY( ']' ):
      if ( !status.use_custom_lcd_filter )
        break;

      status.fw_index++;
      if ( status.fw_index > 4 )
        status.fw_index = 0;
      break;

    case grKEY( '-' ):
      if ( !status.use_custom_lcd_filter )
        break;

      FTC_Manager_RemoveFaceID( handle->cache_manager,
                                handle->scaler.face_id );

      status.filter_weights[status.fw_index]--;
      FT_Library_SetLcdFilterWeights( handle->library,
                                      status.filter_weights );
      break;

    case grKEY( '+' ):
    case grKEY( '=' ):
      if ( !status.use_custom_lcd_filter )
        break;

      FTC_Manager_RemoveFaceID( handle->cache_manager,
                                handle->scaler.face_id );

      status.filter_weights[status.fw_index]++;
      FT_Library_SetLcdFilterWeights( handle->library,
                                      status.filter_weights );
      break;

    default:
      break;
    }

    return ret;
  }


  static void
  write_header( FT_Error  error_code )
  {
    FT_Face      face;
    const char*  basename;
    const char*  format;


    error = FTC_Manager_LookupFace( handle->cache_manager,
                                    handle->scaler.face_id, &face );
    if ( error )
      Fatal( "can't access font file" );

    if ( !status.header )
    {
      basename = ft_basename( handle->current_font->filepathname );

      switch ( error_code )
      {
      case FT_Err_Ok:
        sprintf( status.header_buffer, "%.50s %.50s (file `%.100s')",
                 face->family_name, face->style_name, basename );
        break;
      case FT_Err_Invalid_Pixel_Size:
        sprintf( status.header_buffer, "Invalid pixel size (file `%.100s')",
                 basename );
        break;
      case FT_Err_Invalid_PPem:
        sprintf( status.header_buffer, "Invalid ppem value (file `%.100s')",
                 basename );
        break;
      default:
        sprintf( status.header_buffer, "File `%.100s': error 0x%04x",
                 basename, (FT_UShort)error_code );
        break;
      }

      status.header = status.header_buffer;
    }

    grWriteCellString( display->bitmap, 0, 0,
                       status.header, display->fore_color );

    format = status.encoding != FT_ENCODING_NONE
             ? "at %g points, first char code = 0x%x"
             : "at %g points, first glyph index = %d";

    snprintf( status.header_buffer, 256, format,
              status.ptsize / 64.0, status.Num );

    if ( FT_HAS_GLYPH_NAMES( face ) )
    {
      char*  p;
      int    format_len, gindex, size;


      size = strlen( status.header_buffer );
      p    = status.header_buffer + size;
      size = 256 - size;

      format = ", name = ";
      format_len = strlen( format );

      if ( size >= format_len + 2 )
      {
        gindex = status.Num;
        if ( status.encoding != FT_ENCODING_NONE )
          gindex = FTDemo_Get_Index( handle, status.Num );

        strcpy( p, format );
        if ( FT_Get_Glyph_Name( face, gindex,
                                p + format_len, size - format_len ) )
          *p = '\0';
      }
    }

    status.header = status.header_buffer;
    grWriteCellString( display->bitmap, 0, HEADER_HEIGHT,
                       status.header_buffer, display->fore_color );

    if ( status.use_custom_lcd_filter )
    {
      int             fwi = status.fw_index;
      unsigned char  *fw  = status.filter_weights;


      sprintf( status.header_buffer,
               "%s0x%02X%s%s0x%02X%s%s0x%02X%s%s0x%02X%s%s0x%02X%s",
               fwi == 0 ? "[" : " ", fw[0], fwi == 0 ? "]" : " ",
               fwi == 1 ? "[" : " ", fw[1], fwi == 1 ? "]" : " ",
               fwi == 2 ? "[" : " ", fw[2], fwi == 2 ? "]" : " ",
               fwi == 3 ? "[" : " ", fw[3], fwi == 3 ? "]" : " ",
               fwi == 4 ? "[" : " ", fw[4], fwi == 4 ? "]" : " " );
      grWriteCellString( display->bitmap, 0, 2 * HEADER_HEIGHT,
                         status.header_buffer, display->fore_color );
    }

    grRefreshSurface( display->surface );
  }


  static void
  usage( char*  execname )
  {
    fprintf( stderr,
      "\n"
      "ftview: simple glyph viewer -- part of the FreeType project\n"
      "-----------------------------------------------------------\n"
      "\n" );
    fprintf( stderr,
      "Usage: %s [options] pt font ...\n"
      "\n", execname );
    fprintf( stderr,
      "  pt        The point size for the given resolution.\n"
      "            If resolution is 72dpi, this directly gives the\n"
      "            ppem value (pixels per EM).\n" );
    fprintf( stderr,
      "  font      The font file(s) to display.\n"
      "            For Type 1 font files, ftview also tries to attach\n"
      "            the corresponding metrics file (with extension\n"
      "            `.afm' or `.pfm').\n"
      "\n" );
    fprintf( stderr,
      "  -r R      Use resolution R dpi (default: 72dpi).\n"
      "  -f index  Specify first index to display (default: 0).\n"
      "  -e enc    Specify encoding tag (default: no encoding).\n"
      "            Common values: `unic' (Unicode), `symb' (symbol),\n"
      "            `ADOB' (Adobe standard), `ADBC' (Adobe custom).\n"
      "  -D        Dump cache usage statistics.\n"
      "  -m text   Use `text' for rendering.\n" );
    fprintf( stderr,
      "  -l mode   Change rendering mode (0 <= mode <= %d).\n", N_LCD_MODES );
    fprintf( stderr,
      "  -p        Preload file in memory to simulate memory-mapping.\n"
      "\n" );

    exit( 1 );
  }


  static void
  parse_cmdline( int*    argc,
                 char**  argv[] )
  {
    char*  execname;
    int    option;


    execname = ft_basename( (*argv)[0] );

    while ( 1 )
    {
      option = getopt( *argc, *argv, "Dde:f:L:l:r:m:p" );

      if ( option == -1 )
        break;

      switch ( option )
      {
      case 'd':
        status.debug = 1;
        break;

      case 'D':
        status.dump_cache_stats = 1;
        break;

      case 'e':
        status.encoding = FTDemo_Make_Encoding_Tag( optarg );
        break;

      case 'f':
        status.Num  = atoi( optarg );
        break;

      case 'L':
        status.trace_level = atoi( optarg );
        if ( status.trace_level < 1 || status.trace_level > 7 )
          usage( execname );
        break;

      case 'l':
        status.lcd_mode = atoi( optarg );
        if ( status.lcd_mode < 0 || status.lcd_mode > N_LCD_MODES )
        {
          fprintf( stderr, "argument to `l' must be between 0 and %d\n",
                   N_LCD_MODES );
          exit( 3 );
        }
        break;

      case 'm':
        Text               = (unsigned char*)optarg;
        status.render_mode = RENDER_MODE_TEXT;
        break;

      case 'r':
        status.res = atoi( optarg );
        if ( status.res < 1 )
          usage( execname );
        break;

      case 'p':
        status.preload = 1;
        break;

      default:
        usage( execname );
        break;
      }
    }

    *argc -= optind;
    *argv += optind;

    if ( *argc <= 1 )
      usage( execname );

    status.ptsize = (int)( atof( *argv[0] ) * 64.0 );
    if ( status.ptsize == 0 )
      status.ptsize = 64 * 10;

    (*argc)--;
    (*argv)++;
  }


  int
  main( int    argc,
        char*  argv[] )
  {
    grEvent      event;


    parse_cmdline( &argc, &argv );

#if FREETYPE_MAJOR == 2 && FREETYPE_MINOR == 0 && FREETYPE_PATCH <= 8
    if ( status.debug )
    {
#ifdef FT_DEBUG_LEVEL_TRACE
      FT_SetTraceLevel( trace_any, (FT_Byte)status.trace_level );
#else
      status.trace_level = 0;
#endif
    }
#elif 0
       /* `setenv' and `putenv' is not ANSI and I don't want to mess */
       /* with this portability issue right now...                   */
    if ( status.debug )
    {
      char  temp[32];

      sprintf( temp, "any=%d", status.trace_level );
      setenv( "FT2_DEBUG", temp );
    }
#endif

    /* Initialize engine */
    handle = FTDemo_New( status.encoding );

    FT_Library_SetLcdFilter( handle->library, FT_LCD_FILTER_DEFAULT );

    if ( status.preload )
      FTDemo_Set_Preload( handle, 1 );

    for ( ; argc > 0; argc--, argv++ )
      FTDemo_Install_Font( handle, argv[0] );

    if ( handle->num_fonts == 0 )
      Fatal( "could not find/open any font file" );

    display = FTDemo_Display_New( gr_pixel_mode_rgb24 );
    if ( !display )
      Fatal( "could not allocate display surface" );

    memset( display->fore_color.chroma, 0, 4 );
    memset( display->back_color.chroma, 0xff, 4 );
    grSetTitle( display->surface,
                "FreeType Glyph Viewer - press F1 for help" );

    status.Fail = 0;

    event_font_change( 0 );

    if ( status.lcd_mode >= 0 )
      handle->lcd_mode = status.lcd_mode;

    FTDemo_Update_Current_Flags( handle );

    do
    {
      FTDemo_Display_Clear( display );

      switch ( status.render_mode )
      {
      case RENDER_MODE_ALL:
        error = Render_All( handle->current_font->num_indices,
                            status.Num );
        break;

      case RENDER_MODE_EMBOLDEN:
        error = Render_Embolden( handle->current_font->num_indices,
                                 status.Num );
        break;

      case RENDER_MODE_SLANTED:
        error = Render_Slanted( handle->current_font->num_indices,
                                status.Num );
        break;

      case RENDER_MODE_STROKE:
        error = Render_Stroke( handle->current_font->num_indices,
                               status.Num );
        break;

      case RENDER_MODE_TEXT:
        error = Render_Text( -1, status.Num );
        break;

      case RENDER_MODE_WATERFALL:
        error = Render_Waterfall( status.ptsize );
        break;
      }

      write_header( error );

#if FREETYPE_MAJOR == 2 && FREETYPE_MINOR < 2
      if ( status.dump_cache_stats )
      {
        /* dump simple cache manager statistics */
        fprintf( stderr, "cache manager [ nodes, bytes, average ] = "
                         " [ %d, %ld, %f ]\n",
                         handle->cache_manager->num_nodes,
                         handle->cache_manager->cur_weight,
                         handle->cache_manager->num_nodes > 0
                           ? handle->cache_manager->cur_weight * 1.0 /
                               handle->cache_manager->num_nodes
                           : 0.0 );
      }
#endif

      status.header = 0;
      grListenSurface( display->surface, 0, &event );
    } while ( Process_Event( &event ) == 0 );

    printf( "Execution completed successfully.\n" );
    printf( "Fails = %d\n", status.Fail );

    FTDemo_Display_Done( display );
    FTDemo_Done( handle );
    exit( 0 );      /* for safety reasons */

    return 0;       /* never reached */
  }


/* End */
