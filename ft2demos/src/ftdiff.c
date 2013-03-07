/****************************************************************************/
/*                                                                          */
/*  The FreeType project -- a free and portable quality TrueType renderer.  */
/*                                                                          */
/*  Copyright 2007-2012 by                                                  */
/*  D. Turner, R.Wilhelm, and W. Lemberg                                    */
/*                                                                          */
/*                                                                          */
/*  FTDiff - a simple viewer to compare different hinting modes.            */
/*                                                                          */
/*  Press F1 when running this program to have a list of key-bindings.      */
/*                                                                          */
/****************************************************************************/


#include "ftcommon.h"
#include "common.h"

#include FT_OUTLINE_H
#include FT_LCD_FILTER_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>


  static void
  usage( char*  execname )
  {
    fprintf( stderr,
      "\n"
      "ftdiff: compare font hinting modes -- part of the FreeType project\n"
      "------------------------------------------------------------------\n"
      "\n" );
    fprintf( stderr,
      "Usage: %s [options] font ...\n"
      "\n", execname );
    fprintf( stderr,
      "  font      The font file(s) to display.\n"
      "            For Type 1 font files, ftdiff also tries to attach\n"
      "            the corresponding metrics file (with extension\n"
      "            `.afm' or `.pfm').\n"
      "\n" );
    fprintf( stderr,
      "  -r R         use resolution R dpi (default: 72dpi)\n"
      "  -s S         set character size to S points (default: 16pt)\n"
      "  -f TEXTFILE  change displayed text, using text in TEXTFILE\n"
      "               (in UTF-8 encoding)\n"
      "\n" );

    exit( 1 );
  }


  static void
  panic( const char*  fmt,
         ... )
  {
    va_list  args;


    va_start( args, fmt );
    vfprintf( stderr, fmt, args );
    va_end( args );
    exit( 1 );
  }


  /** DISPLAY ABSTRACTION **/

  typedef enum  _DisplayMode
  {
    DISPLAY_MODE_MONO = 0,
    DISPLAY_MODE_GRAY,
    DISPLAY_MODE_LCD

  } DisplayMode;

  typedef void
  (*Display_drawFunc)( void*        disp,
                       DisplayMode  mode,
                       int          x,
                       int          y,
                       int          width,
                       int          height,
                       int          pitch,
                       void*        buffer );

  typedef void
  (*Display_textFunc)( void*        disp,
                       int          x,
                       int          y,
                       const char*  msg );


  typedef struct  _DisplayRec
  {
    void*             disp;
    Display_drawFunc  disp_draw;
    Display_textFunc  disp_text;

  } DisplayRec, *Display;


  static const char*  default_text =
    "Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Cras sit amet"
    " dui.  Nam sapien. Fusce vestibulum ornare metus. Maecenas ligula orci,"
    " consequat vitae, dictum nec, lacinia non, elit. Aliquam iaculis"
    " molestie neque. Maecenas suscipit felis ut pede convallis malesuada."
    " Aliquam erat volutpat. Nunc pulvinar condimentum nunc. Donec ac sem vel"
    " leo bibendum aliquam. Pellentesque habitant morbi tristique senectus et"
    " netus et malesuada fames ac turpis egestas.\n"
    "\n"
    "Sed commodo. Nulla ut libero sit amet justo varius blandit. Mauris vitae"
    " nulla eget lorem pretium ornare. Proin vulputate erat porta risus."
    " Vestibulum malesuada, odio at vehicula lobortis, nisi metus hendrerit"
    " est, vitae feugiat quam massa a ligula. Aenean in tellus. Praesent"
    " convallis. Nullam vel lacus.  Aliquam congue erat non urna mollis"
    " faucibus. Morbi vitae mauris faucibus quam condimentum ornare. Quisque"
    " sit amet augue. Morbi ullamcorper mattis enim. Aliquam erat volutpat."
    " Morbi nec felis non enim pulvinar lobortis.  Ut libero. Nullam id orci"
    " quis nisl dapibus rutrum. Suspendisse consequat vulputate leo. Aenean"
    " non orci non tellus iaculis vestibulum. Sed neque.\n"
    "\n";


  /***********************************************************************/
  /***********************************************************************/
  /*****                                                             *****/
  /*****               T E X T   R E N D E R I N G                   *****/
  /*****                                                             *****/
  /***********************************************************************/
  /***********************************************************************/

  typedef enum  _HintMode
  {
    HINT_MODE_UNHINTED,
    HINT_MODE_AUTOHINT,
    HINT_MODE_AUTOHINT_LIGHT,
    HINT_MODE_BYTECODE,
    HINT_MODE_MAX

  } HintMode;

  static const char* const  render_mode_names[HINT_MODE_MAX] =
  {
    "unhinted",
    "auto hinter",
    "light auto hinter",
    "native hinter"
  };

  /** RENDER STATE **/

  typedef struct  _ColumnStateRec
  {
    int            use_kerning;
    int            use_deltas;
    int            use_lcd_filter;
    FT_LcdFilter   lcd_filter;
    HintMode       hint_mode;
    DisplayMode    disp_mode;

    int            use_custom_lcd_filter;
    unsigned char  filter_weights[5];
    int            fw_index;

  } ColumnStateRec, *ColumnState;

  typedef struct  _FontFaceRec
  {
    const char*  filepath;
    int          index;

  } FontFaceRec, *FontFace;

  typedef struct  _RenderStateRec
  {
    FT_Library      library;
    const char*     text;
    int             resolution;
    float           char_size;
    int             need_rescale;
    int             col;
    ColumnStateRec  columns[3];
    FontFace        faces;
    int             num_faces;
    int             face_index;
    const char*     filepath;
    const char*     filename;
    FT_Face         face;
    FT_Size         size;
    char**          files;
    char*           message;
    DisplayRec      display;
    char            filepath0[1024];
    char            message0[1024];

  } RenderStateRec, *RenderState;


  static void
  render_state_init( RenderState  state,
                     Display      display )
  {
    memset( state, 0, sizeof ( *state ) );

    state->text         = default_text;
    state->filepath     = state->filepath0;
    state->filename     = "<none>";
    state->filepath0[0] = 0;
    state->resolution   = 72;
    state->char_size    = 16;
    state->display      = display[0];

    state->columns[0].use_kerning           = 1;
    state->columns[0].use_deltas            = 1;
    state->columns[0].use_lcd_filter        = 1;
    state->columns[0].lcd_filter            = FT_LCD_FILTER_DEFAULT;
    state->columns[0].hint_mode             = HINT_MODE_BYTECODE;
    state->columns[0].use_custom_lcd_filter = 0;
    state->columns[0].fw_index              = 2;
    /* freetype default filter weights */
    memcpy( state->columns[0].filter_weights, "\x10\x40\x70\x40\x10", 5 );

    state->columns[1]                       = state->columns[0];
    state->columns[1].hint_mode             = HINT_MODE_AUTOHINT;
    state->columns[1].use_custom_lcd_filter = 1;

    state->columns[2]           = state->columns[0];
    state->columns[2].hint_mode = HINT_MODE_UNHINTED;

    state->col = 1;

    if ( FT_Init_FreeType( &state->library ) != 0 )
      panic( "could not initialize FreeType library. Check your code\n" );
  }


  static void
  render_state_done( RenderState  state )
  {
    if ( state->filepath != state->filepath0 )
    {
      free( (char*)state->filepath );
      state->filepath = state->filepath0;
    }
    state->filepath0[0] = 0;
    state->filename     = 0;

    if ( state->face )
    {
      FT_Done_Face( state->face );
      state->face = NULL;
      state->size = NULL;
    }

    if ( state->library )
    {
      FT_Done_FreeType( state->library );
      state->library = NULL;
    }
  }


  static void
  render_state_set_resolution( RenderState  state,
                               int          resolution )
  {
    state->resolution   = resolution;
    state->need_rescale = 1;
  }


  static void
  render_state_set_size( RenderState  state,
                         float        char_size )
  {
    state->char_size    = char_size;
    state->need_rescale = 1;
  }


  static void
  _render_state_rescale( RenderState  state )
  {
    if ( state->need_rescale && state->size )
    {
      FT_Set_Char_Size( state->face, 0,
                        (FT_F26Dot6)( state->char_size * 64.0 ),
                        0, state->resolution );
      state->need_rescale = 0;
    }
  }


  static void
  render_state_set_files( RenderState  state,
                          char**       files,
                          char*        execname )
  {
    FontFace  faces     = NULL;
    int       num_faces = 0;
    int       max_faces = 0;


    state->files = files;
    for ( ; files[0] != NULL; files++ )
    {
      FT_Face   face;
      int       count;


      error = FT_New_Face( state->library, files[0], -1, &face );
      if ( error )
      {
        fprintf( stderr,
                 "ftdiff: could not open font file `%s'\n", files[0] );
        continue;
      }

      for ( count = 0; count < (int)face->num_faces; count++ )
      {
        if ( num_faces >= max_faces )
        {
          max_faces += ( max_faces >> 1 ) + 8;
          faces = (FontFace)realloc( faces,
                                     max_faces * sizeof ( faces[0] ) );
          if ( faces == NULL )
            panic( "ftdiff: not enough memory\n" );
        }

        faces[num_faces].filepath = files[0];
        faces[num_faces].index    = count;
        num_faces++;
      }

      FT_Done_Face( face );
    }

    state->faces     = faces;
    state->num_faces = num_faces;

    if ( num_faces == 0 )
    {
      fprintf( stderr, "ftdiff: no input font files!\n" );
      usage( execname );
    }

    state->face_index = 0;
  }


  static int
  render_state_set_file( RenderState  state,
                         int          idx )
  {
    const char*  filepath;


    if ( idx < 0 )
      idx = state->num_faces - 1;
    else if ( idx >= state->num_faces )
      idx = 0;

    if ( idx >= state->num_faces )
      return -2;

    state->face_index = idx;
    filepath          = state->faces[idx].filepath;

    if ( state->face )
    {
      FT_Done_Face( state->face );
      state->face         = NULL;
      state->size         = NULL;
      state->need_rescale = 1;
    }

    if ( filepath != NULL && filepath[0] != 0 )
    {
      error = FT_New_Face( state->library, filepath,
                           state->faces[idx].index, &state->face );
      if ( error )
        return -1;

      {
        unsigned int  len = strlen( filepath );
        char*         p;


        if ( len + 1 > sizeof ( state->filepath0 ) )
        {
          state->filepath = (const char*)malloc( len + 1 );
          if ( state->filepath == NULL )
          {
            state->filepath = state->filepath0;
            return -1;
          }
        }
        memcpy( (char*)state->filepath, filepath, len + 1 );
        p = (char*)strrchr( state->filepath, '\\' );
        if ( p == NULL )
          p = (char*)strrchr( state->filepath, '/' );

        state->filename = p ? p + 1 : state->filepath;
      }

      state->size         = state->face->size;
      state->need_rescale = 1;
    }

    return 0;
  }


  /** RENDERING **/

  static void
  render_state_draw( RenderState  state,
                     const char*  text,
                     int          idx,
                     int          x,
                     int          y,
                     int          width,
                     int          height )
  {
    ColumnState  column         = &state->columns[idx];
    const char*  p              = text;
    const char*  p_end          = p + strlen( text );
    long         load_flags     = FT_LOAD_DEFAULT;
    FT_Face      face           = state->face;
    int          left           = x;
    int          right          = x + width;
    int          bottom         = y + height;
    int          line_height;
    FT_UInt      prev_glyph     = 0;
    FT_Pos       prev_rsb_delta = 0;
    FT_Pos       x_origin       = x << 6;
    HintMode     rmode          = column->hint_mode;
    FT_Bool      have_0x0A      = 0;
    FT_Bool      have_0x0D      = 0;


    if ( !face )
      return;

    _render_state_rescale( state );

    if ( column->use_lcd_filter )
      FT_Library_SetLcdFilter( face->glyph->library, column->lcd_filter );

    if ( column->use_custom_lcd_filter )
      FT_Library_SetLcdFilterWeights( face->glyph->library,
                                      column->filter_weights );

    y          += state->size->metrics.ascender / 64;
    line_height = state->size->metrics.height / 64;

    if ( rmode == HINT_MODE_AUTOHINT )
      load_flags = FT_LOAD_FORCE_AUTOHINT;

    if ( rmode == HINT_MODE_AUTOHINT_LIGHT )
      load_flags = FT_LOAD_TARGET_LIGHT;

    if ( rmode == HINT_MODE_UNHINTED )
      load_flags |= FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP;

    while ( 1 )
    {
      int           ch;
      FT_UInt       gindex;
      FT_GlyphSlot  slot = face->glyph;
      FT_Bitmap*    map  = &slot->bitmap;
      int           xmax;


      ch = utf8_next( &p, p_end );

      /* handle newlines */
      if ( ch == 0x0A )
      {
        if ( have_0x0D )
        {
          have_0x0A = 0;
          have_0x0D = 0;
        }
        else
        {
          have_0x0A = 1;

          x_origin = left << 6;
          y       += line_height;
          prev_rsb_delta = 0;
          if ( y >= bottom )
            break;
        }

        continue;
      }
      else if ( ch == 0x0D )
      {
        if ( have_0x0A )
        {
          have_0x0A = 0;
          have_0x0D = 0;
        }
        else
        {
          have_0x0D = 1;

          x_origin = left << 6;
          y       += line_height;
          prev_rsb_delta = 0;
          if ( y >= bottom )
            break;
        }

        continue;
      }
      else
      {
        have_0x0A = 0;
        have_0x0D = 0;
      }

      gindex = FT_Get_Char_Index( state->face, ch );
      error  = FT_Load_Glyph( face, gindex, load_flags );

      if ( error )
        continue;

      if ( column->use_kerning && gindex != 0 && prev_glyph != 0 )
      {
        FT_Vector  vec;
        FT_Int     kerning_mode = FT_KERNING_DEFAULT;


        if ( rmode == HINT_MODE_UNHINTED )
          kerning_mode = FT_KERNING_UNFITTED;

        FT_Get_Kerning( face, prev_glyph, gindex, kerning_mode, &vec );

        x_origin += vec.x;
      }

      if ( column->use_deltas )
      {
        if ( prev_rsb_delta - face->glyph->lsb_delta >= 32 )
          x_origin -= 64;
        else if ( prev_rsb_delta - face->glyph->lsb_delta < -32 )
          x_origin += 64;
      }
      prev_rsb_delta = face->glyph->rsb_delta;

      /* implement sub-pixel positining for un-hinted mode */
      if ( rmode == HINT_MODE_UNHINTED             &&
           slot->format == FT_GLYPH_FORMAT_OUTLINE )
      {
        FT_Pos  shift = x_origin & 63;


        FT_Outline_Translate( &slot->outline, shift, 0 );
      }

      if ( slot->format == FT_GLYPH_FORMAT_OUTLINE )
      {
        FT_BBox  cbox;


        FT_Outline_Get_CBox( &slot->outline, &cbox );
        xmax = ( x_origin + cbox.xMax + 63 ) >> 6;

        FT_Render_Glyph( slot,
                         column->use_lcd_filter ? FT_RENDER_MODE_LCD
                                                : FT_RENDER_MODE_NORMAL );
      }
      else
        xmax = ( x_origin >> 6 ) + slot->bitmap_left + slot->bitmap.width;

      if ( xmax >= right )
      {
        x  = left;
        y += line_height;
        if ( y >= bottom )
          break;

        x_origin       = x << 6;
        prev_rsb_delta = 0;
      }

      {
        DisplayMode  mode = DISPLAY_MODE_MONO;


        if ( slot->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY )
          mode = DISPLAY_MODE_GRAY;
        else if ( slot->bitmap.pixel_mode == FT_PIXEL_MODE_LCD )
          mode = DISPLAY_MODE_LCD;

        state->display.disp_draw( state->display.disp, mode,
                                  ( x_origin >> 6 ) + slot->bitmap_left,
                                  y - slot->bitmap_top,
                                  map->width, map->rows,
                                  map->pitch, map->buffer );
      }
      if ( rmode == HINT_MODE_UNHINTED )
        x_origin += slot->linearHoriAdvance >> 10;
      else
        x_origin += slot->advance.x;

      prev_glyph = gindex;
    }

    /* display footer on this column */
    {
      void*        disp = state->display.disp;
      const char  *msg;
      char         temp[64];


      msg = render_mode_names[column->hint_mode];
      state->display.disp_text( disp, left, bottom + 5, msg );

      if ( !column->use_lcd_filter )
        msg = "gray rendering";
      else if ( column->use_custom_lcd_filter )
      {
        int             fwi = column->fw_index;
        unsigned char  *fw  = column->filter_weights;


        msg = "";

        sprintf( temp,
                 "%s0x%02X%s0x%02X%s0x%02X%s0x%02X%s0x%02X%s",
                 fwi == 0 ? "[" : " ",
                   fw[0],
                 fwi == 0 ? "]" : ( fwi == 1 ? "[" : " " ),
                   fw[1],
                 fwi == 1 ? "]" : ( fwi == 2 ? "[" : " " ),
                   fw[2],
                 fwi == 2 ? "]" : ( fwi == 3 ? "[" : " " ),
                   fw[3],
                 fwi == 3 ? "]" : ( fwi == 4 ? "[" : " " ),
                   fw[4],
                 fwi == 4 ? "]" : " " );
        state->display.disp_text( disp, left, bottom + 15, temp );
      }
      else switch ( column->lcd_filter )
      {
      case FT_LCD_FILTER_NONE:
        msg = "LCD without filtering";
        break;
      case FT_LCD_FILTER_DEFAULT:
        msg = "default LCD filter";
        break;
      case FT_LCD_FILTER_LIGHT:
        msg = "light LCD filter";
        break;
      default:
        msg = "legacy LCD filter";
      }
      state->display.disp_text( disp, left, bottom + 15, msg );

      sprintf( temp, "%s %s",
               column->use_kerning ? "+kern"
                                   : "-kern",
               column->use_deltas ? "+delta"
                                  : "-delta" );
      state->display.disp_text( disp, left, bottom + 25, temp );

      if ( state->col == idx )
        state->display.disp_text( disp, left, bottom + 35, "**************" );
    }
  }


  /***********************************************************************/
  /***********************************************************************/
  /*****                                                             *****/
  /*****                D I S P L A Y                                *****/
  /*****                                                             *****/
  /***********************************************************************/
  /***********************************************************************/

#include "graph.h"
#include "grobjs.h"
#include "grfont.h"

  typedef struct  _ADisplayRec
  {
    grSurface*  surface;
    grBitmap*   bitmap;
    grColor     fore_color;
    grColor     back_color;
    double      gamma;

  } ADisplayRec, *ADisplay;

#define DIM_X  640
#define DIM_Y  480


  static int
  adisplay_init( ADisplay     display,
                 grPixelMode  mode )
  {
    grSurface*  surface;
    grBitmap    bit;


    if ( mode != gr_pixel_mode_gray  &&
         mode != gr_pixel_mode_rgb24 )
      return -1;

    grInitDevices();

    bit.mode  = mode;
    bit.width = DIM_X;
    bit.rows  = DIM_Y;
    bit.grays = 256;

    surface = grNewSurface( 0, &bit );

    if ( !surface )
      return -1;

    display->surface = surface;
    display->bitmap  = &surface->bitmap;
    display->gamma   = 1.0;

    grSetGlyphGamma( display->gamma );

    memset( &display->fore_color, 0, sizeof( grColor ) );
    memset( &display->back_color, 0xff, sizeof( grColor ) );

    return 0;
  }


  static void
  adisplay_clear( ADisplay  display )
  {
    grBitmap*  bit   = display->bitmap;
    int        pitch = bit->pitch;


    if ( pitch < 0 )
      pitch = -pitch;

    if ( bit->mode == gr_pixel_mode_gray )
      memset( bit->buffer, display->back_color.value, pitch * bit->rows );
    else
    {
      unsigned char*  p = bit->buffer;
      int             i, j;


      for ( i = 0; i < bit->rows; i++ )
      {
        for ( j = 0; j < bit->width; j++ )
          memcpy( p + 3 * j, display->back_color.chroma, 3 );

        p += pitch;
      }
    }
  }


  static void
  adisplay_done( ADisplay  display )
  {
    grDoneBitmap( display->bitmap );
    grDoneSurface( display->surface );

    display->bitmap  = NULL;
    display->surface = NULL;

    grDoneDevices();
  }


  static void
  adisplay_draw_glyph( void*        _display,
                       DisplayMode  mode,
                       int          x,
                       int          y,
                       int          width,
                       int          height,
                       int          pitch,
                       void*        buffer )
  {
    ADisplay  display = (ADisplay)_display;
    grBitmap  glyph;


    glyph.width  = width;
    glyph.rows   = height;
    glyph.pitch  = pitch;
    glyph.buffer = (unsigned char*)buffer;
    glyph.grays  = 256;
    glyph.mode   = gr_pixel_mode_mono;

    if ( mode == DISPLAY_MODE_GRAY )
      glyph.mode = gr_pixel_mode_gray;
    else if ( mode == DISPLAY_MODE_LCD )
      glyph.mode = gr_pixel_mode_lcd;

    grBlitGlyphToBitmap( display->bitmap, &glyph,
                         x, y, display->fore_color );
  }


  static void
  adisplay_draw_text( void*        _display,
                      int          x,
                      int          y,
                      const char*  msg )
  {
    ADisplay  adisplay = (ADisplay)_display;


    grWriteCellString( adisplay->bitmap, x, y, msg,
                       adisplay->fore_color );
  }


  static void
  adisplay_change_gamma( ADisplay  display,
                         double    delta )
  {
    display->gamma += delta;
    if ( display->gamma > 3.0 )
      display->gamma = 3.0;
    else if ( display->gamma < 0.0 )
      display->gamma = 0.0;

    grSetGlyphGamma( display->gamma );
  }


  static void
  event_help( RenderState  state )
  {
    ADisplay  display = (ADisplay)state->display.disp;
    grEvent   dummy_event;


    adisplay_clear( display );
    grGotoxy( 0, 0 );
    grSetMargin( 2, 1 );
    grGotobitmap( display->bitmap );

    grWriteln( "FreeType hinting mode comparator - part of the FreeType test suite" );
    grLn();
    grWriteln( "This program displays text using various hinting algorithms." );
    grLn();
    grWriteln( "Use the following keys:" );
    grLn();
    grWriteln( "  F1, ?       display this help screen" );
    grLn();
    grWriteln( "  p, n        select previous/next font" );
    grLn();
    grWriteln( "  1, 2, 3     select left, middle, or right column" );
    grWriteln( "  d           toggle lsb/rsb deltas" );
    grWriteln( "  h           toggle hinting mode" );
    grWriteln( "  k           toggle kerning" );
    grWriteln( "  g, v        adjust gamma value" );
    grWriteln( "  r           toggle rendering mode" );
    grLn();
    grWriteln( "  l           change LCD filter type" );
    grWriteln( "  [, ]        select custom LCD filter weight" );
    grWriteln( "  -, +(=)     adjust selected custom LCD filter weight");
    grLn();
    grWriteln( "  Up, Down    adjust pointsize by 0.5 unit" );
    grWriteln( "  PgUp, PgDn  adjust pointsize by 5 units" );
    grLn();
    grWriteln( "press any key to exit this help screen" );

    grRefreshSurface( display->surface );
    grListenSurface( display->surface, gr_event_key, &dummy_event );
  }


  static void
  event_change_gamma( RenderState  state,
                      double       delta )
  {
    ADisplay  display = (ADisplay)state->display.disp;


    adisplay_change_gamma( display, delta );
    if ( display->gamma == 0.0 )
      sprintf( state->message0, "gamma set to sRGB" );
    else
      sprintf( state->message0, "gamma set to %.1f", display->gamma );

    state->message = state->message0;
  }


  static void
  event_change_size( RenderState  state,
                     double       delta )
  {
    double  char_size = state->char_size;


    char_size += delta;
    if ( char_size < 6.0 )
      char_size = 6.0;
    else if ( char_size > 300.0 )
      char_size = 300.0;

    render_state_set_size( state, char_size );
  }


  static int
  process_event( RenderState  state,
                 grEvent*     event )
  {
    int          ret    = 0;
    ColumnState  column = &state->columns[state->col];


    switch ( event->key )
    {
    case grKeyEsc:
    case grKEY( 'q' ):
      ret = 1;
      break;

    case grKeyF1:
    case grKEY( '?' ):
      event_help( state );
      break;

    case grKeyLeft:
      if ( --state->col < 0 )
        state->col = 2;
      state->message = state->message0;
      sprintf( state->message0, "column %d selected", state->col + 1 );
      break;

    case grKeyRight:
      if ( ++state->col > 2 )
        state->col = 0;
      state->message = state->message0;
      sprintf( state->message0, "column %d selected", state->col + 1 );
      break;

    case grKeyUp:
      event_change_size( state, 0.5 );
      break;

    case grKeyDown:
      event_change_size( state, -0.5 );
      break;

    case grKeyPageUp:
      event_change_size( state, 5. );
      break;

    case grKeyPageDown:
      event_change_size( state, -5. );
      break;

    case grKEY( '1' ):
      state->col     = 0;
      state->message = (char *)"column 1 selected";
      break;

    case grKEY( '2' ):
      state->col     = 1;
      state->message = (char *)"column 2 selected";
      break;

    case grKEY( '3' ):
      state->col     = 2;
      state->message = (char *)"column 3 selected";
      break;

    case grKEY( 'd' ):
      column->use_deltas = !column->use_deltas;
      state->message     = column->use_deltas
                             ? (char *)"using rsb/lsb deltas"
                             : (char *)"ignoring rsb/lsb deltas";
      break;

    case grKEY( 'g' ):
      event_change_gamma( state, 0.1 );
      break;

    case grKEY( 'h' ):
      column->hint_mode =
        (HintMode)( ( column->hint_mode + 1 ) % HINT_MODE_MAX );
      state->message = state->message0;
      sprintf( state->message0, "column %d is %s",
               state->col + 1, render_mode_names[column->hint_mode] );
      break;

    case grKEY( 'k' ):
      column->use_kerning = !column->use_kerning;
      state->message      = column->use_kerning
                              ? (char *)"using kerning"
                              : (char *)"ignoring kerning";
      break;

    case grKEY( 'l' ):
      switch ( column->lcd_filter )
      {
      case FT_LCD_FILTER_NONE:
        column->lcd_filter = FT_LCD_FILTER_DEFAULT;
        state->message     = (char *)"using default LCD filter";
        break;

      case FT_LCD_FILTER_DEFAULT:
        if ( !column->use_custom_lcd_filter )
        {
          column->use_custom_lcd_filter = 1;
          state->message                = (char *)"using custom LCD filter";
        }
        else
        {
          column->use_custom_lcd_filter = 0;
          column->lcd_filter            = FT_LCD_FILTER_LIGHT;
          state->message                = (char *)"using light LCD filter";
        }
        break;

      case FT_LCD_FILTER_LIGHT:
        column->lcd_filter = FT_LCD_FILTER_LEGACY;
        state->message     = (char *)"using legacy LCD filter";
        break;

      case FT_LCD_FILTER_LEGACY:
        column->lcd_filter = FT_LCD_FILTER_NONE;
        state->message     = (char *)"using no LCD filter";
        break;

      default:  /* to satisfy picky compilers */
        break;
      }
      break;

    case grKEY( 'n' ):
      render_state_set_file( state, state->face_index + 1 );
      break;

    case grKEY( 'p' ):
      render_state_set_file( state, state->face_index - 1 );
      break;

    case grKEY( 'r' ):
      column->use_lcd_filter = !column->use_lcd_filter;
      state->message         = state->message0;
      sprintf( state->message0, "column %d is using %s",
               state->col + 1, column->use_lcd_filter ? "LCD filtering"
                                                      : "gray rendering" );
      break;

    case grKEY( 'v' ):
      event_change_gamma( state, -0.1 );
      break;

    case grKEY( '[' ):
      if ( !column->use_custom_lcd_filter )
        break;

      column->fw_index--;
      if ( column->fw_index < 0 )
        column->fw_index = 4;
      break;

    case grKEY( ']' ):
      if ( !column->use_custom_lcd_filter )
        break;

      column->fw_index++;
      if ( column->fw_index > 4 )
        column->fw_index = 0;
      break;

    case grKEY( '-' ):
      if ( !column->use_custom_lcd_filter )
        break;

      column->filter_weights[column->fw_index]--;
      break;

    case grKEY( '+' ):
    case grKEY( '=' ):
      if ( !column->use_custom_lcd_filter )
        break;

      column->filter_weights[column->fw_index]++;
      break;

    default:
      break;
    }

    return ret;
  }


  static char*
  get_option_arg( char*   option,
                  char** *pargv,
                  char**  argend,
                  char*   execname )
  {
    if ( option[2] == 0 )
    {
      char**  argv = *pargv;


      if ( ++argv >= argend )
        usage( execname );
      option = argv[0];
      *pargv = argv;
    }
    else
      option += 2;

    return option;
  }


  static void
  write_message( RenderState  state )
  {
    ADisplay  adisplay = (ADisplay)state->display.disp;


    if ( state->message == NULL )
    {
      FontFace  face = &state->faces[state->face_index];
      int       idx, total;


      idx   = face->index;
      total = 1;
      while ( total + state->face_index < state->num_faces &&
              face[total].filepath == face[0].filepath     )
        total++;

      total += idx;

      state->message = state->message0;
      if ( total > 1 )
        sprintf( state->message0, "%.100s %d/%d @ %5.1fpt",
                 state->filename, idx + 1, total,
                 state->char_size );
      else
        sprintf( state->message0, "%.100s @ %5.1fpt",
                 state->filename,
                 state->char_size );
    }

    grWriteCellString( adisplay->bitmap, 0, DIM_Y - 10, state->message,
                       adisplay->fore_color );

    state->message = NULL;
  }


  int
  main( int     argc,
        char**  argv )
  {
    char**          argend = argv + argc;
    ADisplayRec     adisplay[1];
    RenderStateRec  state[1];
    DisplayRec      display[1];
    int             resolution = -1;
    double          size       = -1;
    const char*     textfile   = NULL;
    char*           text       = (char*)default_text;
    char*           execname   = ft_basename( argv[0] );


    /* Read Options */
    ++argv;
    while ( argv < argend && argv[0][0] == '-' )
    {
      char*  arg = argv[0];


      switch ( arg[1] )
      {
      case 'r':
        arg        = get_option_arg( arg, &argv, argend, execname );
        resolution = atoi( arg );
        break;

      case 's':
        arg  = get_option_arg( arg, &argv, argend, execname );
        size = atof( arg );
        break;

      case 'f':
        arg      = get_option_arg( arg, &argv, argend, execname );
        textfile = arg;
        break;

      default:
        usage( execname );
      }
      argv++;
    }

    if ( argv >= argend )
      usage( execname );

    /* Read Text File, if any */
    if ( textfile != NULL )
    {
      FILE*  tfile = fopen( textfile, "r" );


      if ( tfile == NULL )
        fprintf( stderr, "could not read textfile '%s'\n", textfile );
      else
      {
        long  tsize;


        fseek( tfile, 0, SEEK_END );
        tsize = ftell( tfile );

        fseek( tfile, 0, SEEK_SET );
        text = (char*)malloc( tsize + 1 );

        if ( text != NULL )
        {
          fread( text, tsize, 1, tfile );
          text[tsize] = 0;
        }
        else
        {
          fprintf( stderr, "not enough memory to read `%s'\n", textfile );
          text = (char *)default_text;
        }

        fclose( tfile );
      }
    }

    /* Initialize display */
    if ( adisplay_init( adisplay, gr_pixel_mode_rgb24 ) < 0 )
    {
      fprintf( stderr, "could not initialize display!  Aborting.\n" );
      exit( 1 );
    }
    display->disp      = adisplay;
    display->disp_draw = adisplay_draw_glyph;
    display->disp_text = adisplay_draw_text;

    render_state_init( state, display );

    if ( resolution > 0 )
      render_state_set_resolution( state, resolution );

    if ( size > 0.0 )
      render_state_set_size( state, size );

    render_state_set_files( state, argv, execname );
    render_state_set_file( state, 0 );

    grSetTitle( adisplay->surface, "FreeType Text Proofer, press F1 for help" );

    for (;;)
    {
      grEvent  event;


      adisplay_clear( adisplay );

      render_state_draw( state, text, 0,
                         10,                10, DIM_X / 3 - 15, DIM_Y - 70 );
      render_state_draw( state, text, 1,
                         DIM_X     / 3 + 5, 10, DIM_X / 3 - 15, DIM_Y - 70 );
      render_state_draw( state, text, 2,
                         DIM_X * 2 / 3 + 5, 10, DIM_X / 3 - 15, DIM_Y - 70 );

      write_message( state );
      grRefreshSurface( adisplay->surface );
      grListenSurface( adisplay->surface, 0, &event );
      if ( process_event( state, &event ) )
        break;
    }

    render_state_done( state );
    adisplay_done( adisplay );
    exit( 0 );  /* for safety reasons */

    return 0;   /* never reached */
  }


/* End */
