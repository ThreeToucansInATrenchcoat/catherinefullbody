static Glyph make_glyph(const std::string& value,int size){
 Glyph result;
 cairo_surface_t* measure=cairo_image_surface_create(CAIRO_FORMAT_A8,1,1);
 cairo_t* context=cairo_create(measure);
 cairo_select_font_face(context,"sans-serif",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);
 cairo_set_font_size(context,size);
 cairo_text_extents_t extent{};
 cairo_font_extents_t font{};
 cairo_text_extents(context,value.c_str(),&extent);
 cairo_font_extents(context,&font);
 cairo_destroy(context);
 cairo_surface_destroy(measure);

 result.w=std::clamp(int(std::ceil(extent.x_advance))+4,1,width);
 result.h=std::max(size+8,int(std::ceil(font.height))+4);
 cairo_surface_t* surface=cairo_image_surface_create(CAIRO_FORMAT_A8,result.w,result.h);
 context=cairo_create(surface);
 cairo_select_font_face(context,"sans-serif",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);
 cairo_set_font_size(context,size);
 cairo_set_source_rgba(context,1,1,1,1);
 cairo_move_to(context,1,font.ascent);
 cairo_show_text(context,value.c_str());
 if(cairo_status(context)==CAIRO_STATUS_SUCCESS){
  cairo_surface_flush(surface);
  const auto* bitmap=cairo_image_surface_get_data(surface);
  const int stride=cairo_image_surface_get_stride(surface);
  result.mask.resize(size_t(result.w)*result.h);
  for(int row=0;row<result.h;++row)
   std::memcpy(result.mask.data()+size_t(row)*result.w,bitmap+size_t(row)*stride,result.w);
 }
 cairo_destroy(context);
 cairo_surface_destroy(surface);
 return result;
}
