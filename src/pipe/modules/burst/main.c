#include "modules/api.h"
#include "config.h"

// TODO: redo timing. but it seems down4 is a lot more precise (less pyramid approx?)
#define DOWN 4
#if DOWN==4
#define NUM_LEVELS 4
#else
#define NUM_LEVELS 6
#endif

// the roi callbacks are only needed for the debug outputs. other than that
// the default implementation would work fine for us.
void modify_roi_in(
    dt_graph_t *graph,
    dt_module_t *module)
{
  for(int i=0;i<4+NUM_LEVELS*2;i++)
  {
    module->connector[i].roi.wd = module->connector[i].roi.full_wd;
    module->connector[i].roi.ht = module->connector[i].roi.full_ht;
    module->connector[i].roi.scale = 1.0f;
  }
}

void modify_roi_out(
    dt_graph_t *graph,
    dt_module_t *module)
{
#ifdef DT_BURST_HALFRES_FIT // 2x2 downsampling
  const int block = module->img_param.filters == 9u ? 3 : (module->img_param.filters == 0 ? 2 : 2);
#else // full res
  const int block = module->img_param.filters == 9u ? 3 : (module->img_param.filters == 0 ? 1 : 2);
#endif
  module->connector[2].roi = module->connector[0].roi;
  module->connector[3].roi = module->connector[0].roi;
  for(int i=0;i<NUM_LEVELS;i++)
  {
    module->connector[4+i].roi = module->connector[4+i-1].roi;
    int scale = i == 0 ? block : DOWN;
    module->connector[4+i].roi.full_wd /= scale;
    module->connector[4+i].roi.full_ht /= scale;
    module->connector[8+i] = module->connector[4+i];
  }
}

// input connectors: fixed raw file
//                   to-be-warped raw file
// output connector: warped raw file
void
create_nodes(
    dt_graph_t  *graph,
    dt_module_t *module)
{
  // connect each mosaic input to half, generate grey lum map for both input images
  // by a sequence of half, down4, down4, down4 kernels.
  // then compute distance (dist kernel) coarse to fine, merge best offsets (merge kernel),
  // input best coarse offsets to next finer level, and finally output offsets on finest scale.
  //
  dt_roi_t roi[NUM_LEVELS+1] = {module->connector[0].roi};
#ifdef DT_BURST_HALFRES_FIT // 2x2 down
  const int block = module->img_param.filters == 9u ? 3 : (module->img_param.filters == 0 ? 2 : 2);
#else // full res
  const int block = module->img_param.filters == 9u ? 3 : (module->img_param.filters == 0 ? 1 : 2);
#endif
  for(int i=1;i<=NUM_LEVELS;i++)
  {
    int scale = i == 1 ? block : DOWN;
    roi[i] = roi[i-1];
    roi[i].full_wd /= scale;
    roi[i].full_ht /= scale;
    roi[i].wd /= scale;
    roi[i].ht /= scale;
    roi[i].x  /= scale;
    roi[i].y  /= scale;
  }

  dt_token_t fmt_img = dt_token("f16");//ui8"); // or f16
  dt_token_t fmt_dst = dt_token("f16");//ui8");//ui8"); // or f16

  int id_down[2][NUM_LEVELS] = {0};
  for(int k=0;k<2;k++)
  {
    assert(graph->num_nodes < graph->max_nodes);
    id_down[k][0] = graph->num_nodes++;
    graph->node[id_down[k][0]] = (dt_node_t) {
      .name   = dt_token("burst"),
      .kernel = dt_token("half"),
      .module = module,
      .wd     = roi[1].wd,
      .ht     = roi[1].ht,
      .dp     = 1,
      .num_connectors = 2,
      .connector = {{
        .name   = dt_token("input"),
        .type   = dt_token("read"),
        .chan   = module->img_param.filters == 0 ? dt_token("rgba") : dt_token("rggb"),
        .format = module->connector[k].format,
        .roi    = roi[0],
        .connected_mi = -1,
      },{
        .name   = dt_token("output"),
        .type   = dt_token("write"),
        .chan   = dt_token("y"),
        .format = fmt_img,
        .roi    = roi[1],
      }},
      .push_constant_size = sizeof(uint32_t),
      .push_constant = { module->img_param.filters },
    };
    dt_connector_copy(graph, module, k, id_down[k][0], 0);

    for(int i=1;i<NUM_LEVELS;i++)
    {
      assert(graph->num_nodes < graph->max_nodes);
      id_down[k][i] = graph->num_nodes++;
      graph->node[id_down[k][i]] = (dt_node_t) {
        .name   = dt_token("burst"),
#if DOWN==4
        .kernel = dt_token("down4"),
#else
        .kernel = dt_token("down2"),
#endif
        .module = module,
        .wd     = roi[i+1].wd,
        .ht     = roi[i+1].ht,
        .dp     = 1,
        .num_connectors = 2,
        .connector = {{
          .name   = dt_token("input"),
          .type   = dt_token("read"),
          .chan   = dt_token("y"),
          .format = fmt_img,
          .roi    = roi[i],
          .connected_mi = -1,
        },{
          .name   = dt_token("output"),
          .type   = dt_token("write"),
          .chan   = dt_token("y"),
          .format = fmt_img,
          .roi    = roi[i+1],
        }},
      };
      // TODO: if(rgba && i==0) connector_copy as above
      CONN(dt_node_connect(graph, id_down[k][i-1], 1, id_down[k][i], 0));
    }
  }

  // for debug outputs:
  int id_off[NUM_LEVELS] = {0};

  // connect uv-diff and merge:
  int id_offset = -1;
  for(int i=NUM_LEVELS-1;i>=0;i--) // all depths/coarseness levels, starting at coarsest
  {
    // for all offsets in search window (using array connectors)
    assert(graph->num_nodes < graph->max_nodes);
    int id_dist = graph->num_nodes++;
    graph->node[id_dist] = (dt_node_t) {
      .name   = dt_token("burst"),
      .kernel = dt_token("dist"),
      .module = module,
      .wd     = roi[i+1].wd,
      .ht     = roi[i+1].ht,
      .dp     = 25,
      .num_connectors = 4,
      .connector = {{
        .name   = dt_token("input"),
        .type   = dt_token("read"),
        .chan   = dt_token("y"),
        .format = fmt_img,
        .roi    = roi[i+1],
        .connected_mi = -1,
      },{
        .name   = dt_token("warped"),
        .type   = dt_token("read"),
        .chan   = dt_token("y"),
        .format = fmt_img,
        .roi    = roi[i+1],
        .connected_mi = -1,
      },{
        .name   = dt_token("offset"),
        .type   = dt_token("read"),
        .chan   = id_offset >= 0 ? dt_token("rg")  : dt_token("y"),
        .format = id_offset >= 0 ? dt_token("f16") : fmt_img,
        .roi    = id_offset >= 0 ? roi[i+2] : roi[i+1],
        .flags  = s_conn_smooth,
        .connected_mi = -1,
      },{
        .name   = dt_token("output"),
        .type   = dt_token("write"),
        .chan   = dt_token("y"),
        .format = fmt_dst,
        .roi    = roi[i+1],
        .array_length = 25,
      }},
      .push_constant_size = sizeof(uint32_t),
      .push_constant = { id_offset >= 0 ? DOWN: 0 },
    };
    CONN(dt_node_connect(graph, id_down[0][i], 1, id_dist, 0));
    CONN(dt_node_connect(graph, id_down[1][i], 1, id_dist, 1));
    if(id_offset >= 0)
      CONN(dt_node_connect(graph, id_offset, 2, id_dist, 2));
    else // need to connect a dummy
      CONN(dt_node_connect(graph, id_down[0][i], 1, id_dist, 2));

    // blur output of dist node by tile size (depending on noise radius 16x16, 32x32 or 64x64?)
    // grab module parameters, would need to trigger re-create_nodes on change:
    const int blur = ((float*)module->param)[2+i];
    // const int id_blur = dt_api_blur(graph, module, id_dist, 3, blur);
    const int id_blur = dt_api_blur_sub(graph, module, id_dist, 3, 0, 0, blur);

    // merge output of blur node using "merge" (<off0, <off1, >merged)
    assert(graph->num_nodes < graph->max_nodes);
    const int id_merge = graph->num_nodes++;
    graph->node[id_merge] = (dt_node_t) {
      .name   = dt_token("burst"),
      .kernel = dt_token("merge"),
      .module = module,
      .wd     = roi[i+1].wd,
      .ht     = roi[i+1].ht,
      .dp     = 1,
      .num_connectors = 4,
      .connector = {{
        .name   = dt_token("dist"),
        .type   = dt_token("read"),
        .chan   = dt_token("y"),
        .format = fmt_dst,
        .roi    = roi[i+1],
        .flags  = s_conn_smooth,
        .connected_mi = -1,
        .array_length = 25,
      },{
        .name   = dt_token("coff"),
        .type   = dt_token("read"),
        .chan   = id_offset >= 0 ? dt_token("rg")  : dt_token("y"),
        .format = id_offset >= 0 ? dt_token("f16") : fmt_img,
        .roi    = id_offset >= 0 ? roi[i+2] : roi[i+1],
        .flags  = s_conn_smooth,
        .connected_mi = -1,
      },{
        .name   = dt_token("merged"),
        .type   = dt_token("write"),
        .chan   = dt_token("rg"),
        .format = dt_token("f16"),
        .roi    = roi[i+1],
      },{
        .name   = dt_token("resid"),
        .type   = dt_token("write"),
        .chan   = dt_token("r"),
        .format = dt_token("f16"),
        .roi    = roi[i+1],
      }},
      .push_constant_size = 2*sizeof(uint32_t),
      .push_constant = { id_offset >= 0 ? DOWN: 0, i },
    };
    id_off[i] = id_merge;

    CONN(dt_node_connect(graph, id_blur, 1, id_merge, 0));
    // dt_connector_copy(graph, module, 8+i, id_blur, 1); // XXX DEBUG see distances
    // connect coarse offset buffer from previous level:
    if(id_offset >= 0)
      CONN(dt_node_connect(graph, id_offset, 2, id_merge, 1));
    else // need to connect a dummy
      CONN(dt_node_connect(graph, id_down[1][i], 1, id_merge, 1));
    // remember our merged output as offset for next finer scale:
    id_offset = id_merge;
    // id_offset is the final merged offset buffer on this level, ready to be
    // used as input by the next finer level, if any
  }
  // id_offset is now our finest offset buffer, to be used to warp the second
  // raw image to match the first.
  // note that this buffer has dimensions roi[1], i.e. it is still half sized as compared
  // to the full raw image. it is thus large enough to move around full bayer/xtrans blocks,
  // but would need refinement if more fidelity is required (as input for
  // splatting super res demosaic for instance)

#if 0 // DEBUG: connect pyramid debugging output images
  for(int i=0;i<NUM_LEVELS;i++)
  {
    // connect unwarped input buffer, downscaled:
    dt_connector_copy(graph, module, 4+i, id_down[0][i], 1);
    const int coff = 0;
    if(coff && (i==NUM_LEVELS-1))
    {
      dt_connector_copy(graph, module, 8+i, id_down[1][i], 1);
      continue;
    }
    // connect warp node and warp downscaled buffer:
    assert(graph->num_nodes < graph->max_nodes);
    const int id_warp = graph->num_nodes++;
    graph->node[id_warp] = (dt_node_t) {
      .name   = dt_token("burst"),
      .kernel = dt_token("warp"),
      .module = module,
      .wd     = roi[i+1].wd,
      .ht     = roi[i+1].ht,
      .dp     = 1,
      .num_connectors = 3,
      .connector = {{
        .name   = dt_token("input"),
        .type   = dt_token("read"),
        .chan   = dt_token("y"),
        .format = dt_token("f16"),
        .roi    = roi[i+1],
        .connected_mi = -1,
      },{
        .name   = dt_token("offset"),
        .type   = dt_token("read"),
        .chan   = dt_token("rg"),
        .format = dt_token("f16"),
        .roi    = roi[i+1+coff],
        // .flags  = s_conn_smooth,
        .connected_mi = -1,
      },{
        .name   = dt_token("output"),
        .type   = dt_token("write"),
        .chan   = dt_token("y"),
        .format = dt_token("f16"),
        .roi    = roi[i+1],
      }},
      .push_constant_size = 2*sizeof(uint32_t),
      .push_constant = { 0, coff ? DOWN : 1 },
    };
    CONN(dt_node_connect(graph, id_down[1][i],  1, id_warp, 0));
    CONN(dt_node_connect(graph, id_off[i+coff], 2, id_warp, 1));
    dt_connector_copy(graph, module, 8+i, id_warp, 2);
  }
#else
#if 1
  for(int i=0;i<NUM_LEVELS;i++)
  { // visualise offsets
    dt_connector_copy(graph, module, 4+i, id_down[0][i], 1);
#if 0 // motion vectors:
    assert(graph->num_nodes < graph->max_nodes);
    const int id_visn = graph->num_nodes++;
    graph->node[id_visn] = (dt_node_t) {
      .name   = dt_token("burst"),
      .kernel = dt_token("visn"),
      .module = module,
      .wd     = roi[i+1].wd,
      .ht     = roi[i+1].ht,
      .dp     = 1,
      .num_connectors = 2,
      .connector = {{
        .name   = dt_token("offset"),
        .type   = dt_token("read"),
        .chan   = dt_token("rg"),
        .format = dt_token("f16"),
        .roi    = roi[i+1],
        .connected_mi = -1,
      },{
        .name   = dt_token("output"),
        .type   = dt_token("write"),
        .chan   = dt_token("rgba"),
        .format = dt_token("f16"),
        .roi    = roi[i+1],
      }},
    };
    CONN(dt_node_connect(graph, id_off[i], 2, id_visn, 0));
    dt_connector_copy(graph, module, 8+i, id_visn, 1);
#else // plain distance residual:
    dt_connector_copy(graph, module, 8+i, id_off[i], 3);
#endif
  }
#endif
#endif

  assert(graph->num_nodes < graph->max_nodes);
  const int id_warp = graph->num_nodes++;
  graph->node[id_warp] = (dt_node_t) {
    .name   = dt_token("burst"),
    .kernel = dt_token("warp"),
    .module = module,
    .wd     = roi[0].wd,
    .ht     = roi[0].ht,
    .dp     = 1,
    .num_connectors = 3,
    .connector = {{
      .name   = dt_token("input"),
      .type   = dt_token("read"),
      .chan   = module->img_param.filters == 0 ? dt_token("rgba") : dt_token("rggb"),
      .format = dt_token("f16"),
      .roi    = roi[0],
      .connected_mi = -1,
    },{
      .name   = dt_token("offset"),
      .type   = dt_token("read"),
      .chan   = dt_token("rg"),
      .format = dt_token("f16"),
      .roi    = roi[1],
      .flags  = s_conn_smooth,
      .connected_mi = -1,
    },{
      .name   = dt_token("output"),
      .type   = dt_token("write"),
      .chan   = module->img_param.filters == 0 ? dt_token("rgba") : dt_token("rggb"),
      .format = dt_token("f16"),
      .roi    = roi[0],
    }},
    .push_constant_size = 2*sizeof(uint32_t),
    .push_constant = {
      module->img_param.filters,
      block,
    },
  };
  dt_connector_copy(graph, module, 1, id_warp, 0);
  CONN(dt_node_connect(graph, id_offset, 2, id_warp, 1));
  dt_connector_copy(graph, module, 2, id_warp, 2);
#if 0
  dt_connector_copy(graph, module, 3, id_off[1], 3); // XXX
  graph->node[id_off[1]].connector[3].roi = roi[2]; // XXX FIXME: this is shit
#else
  dt_connector_copy(graph, module, 3, id_off[0], 3);  // full res mask
  graph->node[id_off[0]].connector[3].roi = roi[1]; // XXX FIXME: this is shit
#endif
#undef NUM_LEVELS
}
