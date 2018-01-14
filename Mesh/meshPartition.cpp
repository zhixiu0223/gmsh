// Gmsh - Copyright (C) 1997-2018 C. Geuzaine, J.-F. Remacle
//
// See the LICENSE.txt file for license information. Please report all
// bugs and problems to the public mailing list <gmsh@onelab.info>.
//
// Contributed by Anthony Royer.

#include "GmshConfig.h"
#include "GmshMessage.h"

#if defined(HAVE_METIS)

#include <vector>
#include <set>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <limits>
#include <stack>
#include <cstdlib>
#include <map>

#if __cplusplus >= 201103L
#include <unordered_map>
#define hashmap std::unordered_map
#define hashmapface std::unordered_map\
  <MFace, std::vector<std::pair<MElement*, std::vector<unsigned int> > >,\
   Hash_Face, Equal_Face>
#define hashmapedge std::unordered_map\
  <MEdge, std::vector<std::pair<MElement*, std::vector<unsigned int> > >,\
   Hash_Edge, Equal_Edge>
#define hashmapvertex std::unordered_map\
  <MVertex*, std::vector<std::pair<MElement*, std::vector<unsigned int> > > >
#else
#define hashmap std::map
#define hashmapface std::map\
  <MFace, std::vector<std::pair<MElement*, std::vector<unsigned int> > >,\
   Equal_Face>
#define hashmapedge std::map\
  <MEdge, std::vector<std::pair<MElement*, std::vector<unsigned int> > >,\
   Equal_Edge>
#define hashmapvertex std::map\
  <MVertex*, std::vector<std::pair<MElement*, std::vector<unsigned int> > > >
#endif

#include "OS.h"
#include "Context.h"
#include "partitionRegion.h"
#include "partitionFace.h"
#include "partitionEdge.h"
#include "partitionVertex.h"
#include "ghostRegion.h"
#include "ghostFace.h"
#include "ghostEdge.h"
#include "MFaceHash.h"
#include "MEdgeHash.h"
#include "GModel.h"
#include "MTriangle.h"
#include "MQuadrangle.h"
#include "MTetrahedron.h"
#include "MHexahedron.h"
#include "MPrism.h"
#include "MPyramid.h"
#include "MTrihedron.h"
#include "MElementCut.h"
#include "MPoint.h"

extern "C" {
#include <metis.h>
}

// Graph of the mesh for partitioning purposes.
class Graph
{
 private:
  // The GModel
  GModel *_model;
  // The number of partitions
  unsigned int _nparts;
  // The number of elements
  unsigned int _ne;
  // The number of nodes
  unsigned int _nn;
  // The dimension of the mesh
  unsigned int _dim;
  // The list of nodes belonging to the ith element of the mesh are stored in
  // consecutive locations of eind starting at position eptr[i] up to (but not
  // including) position eptr[i+1]. The size of the eind array is of size equal
  // to the sum of the number of nodes in all the elements of the mesh.
  unsigned int *_eind;
  // The size of the eptr array is n + 1, where n is the number of elements in
  // the mesh.
  unsigned int *_eptr;
  // The metis graph structure
  unsigned int *_xadj, *_adjncy;
  // Elements corresponding to each graph elements in eptr
  MElement **_element;
  // Vertices corresponding to each graph vertices in eptr
  int *_vertex;
  // The width associated to each elements of eptr
  unsigned int *_vwgt;
  // The partitions output from the partitioner
  unsigned int *_partition;
 public:
  Graph(GModel * const model)
    : _model(model), _nparts(0), _ne(0), _nn(0), _dim(0), _eind(0), _eptr(0),
    _xadj(0), _adjncy(0), _element(0), _vertex(0), _vwgt(0), _partition(0)
  {
  }
  void fillDefaultWeights()
  {
    _vwgt = new unsigned int[_ne];
    for(unsigned int i = 0; i < _ne; i++){
      if(!_element[i]){
        _vwgt[i] = 1;
        continue;
      }
      switch (_element[i]->getType()){
      case TYPE_TRI: _vwgt[i] = CTX::instance()->mesh.partitionTriWeight; break;
      case TYPE_QUA: _vwgt[i] = CTX::instance()->mesh.partitionQuaWeight; break;
      case TYPE_TET: _vwgt[i] = CTX::instance()->mesh.partitionTetWeight; break;
      case TYPE_PYR: _vwgt[i] = CTX::instance()->mesh.partitionPyrWeight; break;
      case TYPE_PRI: _vwgt[i] = CTX::instance()->mesh.partitionPriWeight; break;
      case TYPE_HEX: _vwgt[i] = CTX::instance()->mesh.partitionHexWeight; break;
      default: _vwgt[i] = 1; break;
      }
    }
  }
  ~Graph()
  {
    clear();
  }
  unsigned int nparts() const { return _nparts; };
  unsigned int ne() const { return _ne; };
  unsigned int nn() const { return _nn; };
  unsigned int dim() const { return _dim; };
  unsigned int eind(unsigned int i) const { return _eind[i]; };
  unsigned int *eind() const { return _eind; };
  unsigned int eptr(unsigned int i) const { return _eptr[i]; };
  unsigned int *eptr() const { return _eptr; };
  unsigned int xadj(unsigned int i) const { return _xadj[i]; };
  unsigned int *xadj() const { return _xadj; };
  unsigned int adjncy(unsigned int i) const { return _adjncy[i]; };
  unsigned int *adjncy() const { return _adjncy; };
  MElement* element(unsigned int i) const { return _element[i]; };
  int vertex(unsigned int i) const { return _vertex[i]; };
  unsigned int *vwgt() const { return _vwgt; };
  unsigned int partition(unsigned int i) const { return _partition[i]; };
  unsigned int *partition() const { return _partition; };
  void nparts(unsigned int nparts) { _nparts = nparts; };
  void ne(unsigned int ne) { _ne = ne; };
  void nn(unsigned int nn) { _nn = nn; };
  void dim(unsigned int dim) { _dim = dim; };
  void eindResize(unsigned int size)
  {
    _eind = new unsigned int[size];
    for(unsigned int i = 0; i < size; i++) _eind[i] = 0;
  }
  void eind(unsigned int i, unsigned int eind) { _eind[i] = eind; };
  void eptrResize(unsigned int size)
  {
    _eptr = new unsigned int[size];
    for(unsigned int i = 0; i < size; i++) _eptr[i] = 0;
  }
  void eptr(unsigned int i, unsigned int eptr) { _eptr[i] = eptr; };
  void xadjResize(unsigned int size)
  {
    _xadj = new unsigned int[size];
    for(unsigned int i = 0; i < size; i++) _xadj[i] = 0;
  }
  void xadj(unsigned int i, unsigned int xadj) { _xadj[i] = xadj; };
  void adjncyResize(unsigned int size)
  {
    _adjncy = new unsigned int[size];
    for(unsigned int i = 0; i < size; i++) _adjncy[i] = 0;
  }
  void adjncy(unsigned int i, unsigned int adjncy) { _adjncy[i] = adjncy; };
  void elementResize(unsigned int size)
  {
    _element = new MElement*[size];
    for(unsigned int i = 0; i < size; i++) _element[i] = 0;
  }
  void element(unsigned int i, MElement* element) { _element[i] = element; };
  void vertexResize(unsigned int size)
  {
    _vertex = new int[size];
    for(unsigned int i = 0; i < size; i++) _vertex[i] = -1;
  }
  void vertex(unsigned int i, int vertex) { _vertex[i] = vertex; };
  void vwgt(unsigned int *vwgt) { _vwgt = vwgt; };
  void partition(unsigned int *partition) { _partition = partition; };
  void clear()
  {
    if(_eind){
      delete[] _eind;
      _eind = 0;
    }
    if(_eptr){
      delete[] _eptr;
      _eptr = 0;
    }
    if(_xadj){
      delete[] _xadj;
      _xadj = 0;
    }
    if(_adjncy){
      delete[] _adjncy;
      _adjncy = 0;
    }
    if(_element){
      delete[] _element;
      _element = 0;
    }
    if(_vertex){
      delete[] _vertex;
      _vertex = 0;
    }
    if(_vwgt){
      delete[] _vwgt;
      _vwgt = 0;
    }
    if(_partition){
      delete[] _partition;
      _partition = 0;
    }
  }
  void clearDualGraph()
  {
    if(_xadj){
      delete[] _xadj;
      _xadj = 0;
    }
    if(_adjncy){
      delete[] _adjncy;
      _adjncy = 0;
    }
  }

  std::vector< std::set<MElement*> > getBoundaryElements(int size = 0)
  {
    std::vector< std::set<MElement*> > elements((size ? size : _nparts), std::set<MElement*>());
    for(unsigned int i = 0; i < _ne; i++){
      for(unsigned int j = _xadj[i]; j < _xadj[i+1]; j++){
        if(_partition[i] != _partition[_adjncy[j]]){
          if(_element[i]->getDim() == (int)_dim){
            elements[_partition[i]].insert(_element[i]);
          }
        }
      }
    }

    return elements;
  }

  void assignGhostCells()
  {
    std::vector<GEntity*> ghostEntities(_nparts, NULL);
    for(unsigned int i = 0; i < _nparts; i++){
      switch (_dim) {
        case 1:
          ghostEntities[i] = new ghostEdge(_model, _model->getMaxElementaryNumber(1)+1, i);
          _model->add(static_cast<ghostEdge*>(ghostEntities[i]));
          break;
        case 2:
          ghostEntities[i] = new ghostFace(_model, _model->getMaxElementaryNumber(2)+1, i);
          _model->add(static_cast<ghostFace*>(ghostEntities[i]));
          break;
        case 3:
          ghostEntities[i] = new ghostRegion(_model, _model->getMaxElementaryNumber(3)+1, i);
          _model->add(static_cast<ghostRegion*>(ghostEntities[i]));
          break;
        default:
          break;
      }
    }

    for(unsigned int i = 0; i < _ne; i++){
      std::set<short> ghostCellsPartition;
      for(unsigned int j = _xadj[i]; j < _xadj[i+1]; j++){
        if(_partition[i] != _partition[_adjncy[j]] &&
           ghostCellsPartition.find(_partition[_adjncy[j]]) == ghostCellsPartition.end()){
          if(_element[i]->getDim() == (int)_dim){
            switch (_dim) {
              case 1:
                static_cast<ghostEdge*>(ghostEntities[_partition[_adjncy[j]]])->
                addElement(_element[i]->getType(), _element[i], _partition[i]);
                break;
              case 2:
                static_cast<ghostFace*>(ghostEntities[_partition[_adjncy[j]]])->
                addElement(_element[i]->getType(), _element[i], _partition[i]);
                break;
              case 3:
                static_cast<ghostRegion*>(ghostEntities[_partition[_adjncy[j]]])->
                addElement(_element[i]->getType(), _element[i], _partition[i]);
                break;
              default:
                break;
            }
            ghostCellsPartition.insert(_partition[_adjncy[j]]);
          }
        }
      }
    }
  }
};

template <class ITERATOR>
static void fillElementsToNodesMap(Graph &graph, const GEntity *const entity,
                                   int &eptrIndex, int &eindIndex, int &numVertex,
                                   ITERATOR it_beg, ITERATOR it_end)
{
  for(ITERATOR it = it_beg; it != it_end; ++it){
    const int numVertices = (*it)->getNumPrimaryVertices();
    graph.element(eptrIndex, *it);
    eptrIndex++;
    graph.eptr(eptrIndex, graph.eptr(eptrIndex-1) + numVertices);
    for(int i = 0; i < numVertices; i++){
      if(graph.vertex((*it)->getVertex(i)->getNum()-1) == -1){
        graph.vertex((*it)->getVertex(i)->getNum()-1, numVertex);
        numVertex++;
      }
      graph.eind(eindIndex, graph.vertex((*it)->getVertex(i)->getNum()-1));
      eindIndex++;
    }
  }
}

static int getSizeOfEind(const GModel *const model)
{
  int size = 0;
  // Loop over regions
  for(GModel::const_riter it = model->firstRegion(); it != model->lastRegion(); ++it){
    size += 4*(*it)->tetrahedra.size();
    size += 8*(*it)->hexahedra.size();
    size += 6*(*it)->prisms.size();
    size += 5*(*it)->pyramids.size();
    size += 4*(*it)->trihedra.size();
  }

  // Loop over faces
  for(GModel::const_fiter it = model->firstFace(); it != model->lastFace(); ++it){
    size += 3*(*it)->triangles.size();
    size += 4*(*it)->quadrangles.size();
  }

  // Loop over edges
  for(GModel::const_eiter it = model->firstEdge(); it != model->lastEdge(); ++it){
    size += 2*(*it)->lines.size();
  }

  // Loop over vertices
  for(GModel::const_viter it = model->firstVertex(); it != model->lastVertex(); ++it){
    size += 1*(*it)->points.size();
  }

  return size;
}

static int getSizeOfEind(const GEntity *const entity)
{
  int size = 0;

  if(entity->dim() == 3){
    size += 4*static_cast<const GRegion*>(entity)->tetrahedra.size();
    size += 8*static_cast<const GRegion*>(entity)->hexahedra.size();
    size += 6*static_cast<const GRegion*>(entity)->prisms.size();
    size += 5*static_cast<const GRegion*>(entity)->pyramids.size();
    size += 4*static_cast<const GRegion*>(entity)->trihedra.size();
  }
  else if(entity->dim() == 2){
    size += 3*static_cast<const GFace*>(entity)->triangles.size();
    size += 4*static_cast<const GFace*>(entity)->quadrangles.size();
  }
  else if(entity->dim() == 1){
    size += 2*static_cast<const GEdge*>(entity)->lines.size();
  }
  else if(entity->dim() == 0){
    size += 1*static_cast<const GVertex*>(entity)->points.size();
  }

  return size;
}

static bool isPeriodic(const GModel *const model)
{
  std::vector<GEntity*> entities;
  model->getEntities(entities);

  for(unsigned int i = 0; i < entities.size(); i++){
    if(entities[i]->correspondingVertices.size() != 0) return true;
  }

  return false;
}

// Creates a mesh data structure used by Metis routines. Returns: 0 = success, 1
// = no elements found, 2 = error.
static int MakeGraph(GModel *const model, Graph &graph, int selectDim)
{
  int eindSize = 0;
  if(selectDim < 0){
    graph.ne(model->getNumMeshElements());
    graph.nn(model->getNumMeshVertices());
    graph.dim(model->getDim());
    graph.elementResize(graph.ne());
    graph.vertexResize(model->getMaxVertexNumber());
    graph.eptrResize(graph.ne()+1);
    graph.eptr(0,0);
    eindSize = getSizeOfEind(model);
    graph.eindResize(eindSize);
  }
  else{
    GModel* tmp = new GModel();
    std::vector<GEntity*> entities;
    model->getEntities(entities);
    
    std::set<MVertex*> vertices;
    for(unsigned int  i = 0; i < entities.size(); i++){
      if(entities[i]->dim() == selectDim){
        switch (entities[i]->dim()) {
          case 3:
            tmp->add(static_cast<GRegion*>(entities[i]));
            break;
          case 2:
            tmp->add(static_cast<GFace*>(entities[i]));
            break;
          case 1:
            tmp->add(static_cast<GEdge*>(entities[i]));
            break;
          case 0:
            tmp->add(static_cast<GVertex*>(entities[i]));
            break;
          default:
            break;
        }
        for(unsigned int j = 0; j < entities[i]->getNumMeshElements(); j++){
          for(unsigned int k = 0; k < entities[i]->getMeshElement(j)->getNumVertices() ; k++){
            vertices.insert(entities[i]->getMeshElement(j)->getVertex(k));
          }
        }
      }
    }
    
    graph.ne(tmp->getNumMeshElements());
    graph.nn(vertices.size());
    graph.dim(tmp->getDim());
    graph.elementResize(graph.ne());
    graph.vertexResize(model->getMaxVertexNumber());
    graph.eptrResize(graph.ne()+1);
    graph.eptr(0,0);
    eindSize = getSizeOfEind(tmp);
    graph.eindResize(eindSize);
    
    tmp->remove();
    delete tmp;
  }

  int eptrIndex = 0;
  int eindIndex = 0;
  int numVertex = 0;

  if(graph.ne() == 0){
    Msg::Error("No mesh elements were found");
    return 1;
  }
  if(graph.dim() == 0){
    Msg::Error("Cannot partition a point");
    return 1;
  }

  // Loop over regions
  if(selectDim < 0 || selectDim == 3){
    for(GModel::const_riter it = model->firstRegion(); it != model->lastRegion(); ++it){
      const GRegion *r = *it;
      fillElementsToNodesMap(graph, r, eptrIndex, eindIndex, numVertex,
                             r->tetrahedra.begin(), r->tetrahedra.end());
      fillElementsToNodesMap(graph, r, eptrIndex, eindIndex, numVertex,
                             r->hexahedra.begin(), r->hexahedra.end());
      fillElementsToNodesMap(graph, r, eptrIndex, eindIndex, numVertex,
                             r->prisms.begin(), r->prisms.end());
      fillElementsToNodesMap(graph, r, eptrIndex, eindIndex, numVertex,
                             r->pyramids.begin(), r->pyramids.end());
      fillElementsToNodesMap(graph, r, eptrIndex, eindIndex, numVertex,
                             r->trihedra.begin(), r->trihedra.end());
    }
  }

  // Loop over faces
  if(selectDim < 0 || selectDim == 2){
    for(GModel::const_fiter it = model->firstFace(); it != model->lastFace(); ++it){
      const GFace *f = *it;

      fillElementsToNodesMap(graph, f, eptrIndex, eindIndex, numVertex,
                             f->triangles.begin(), f->triangles.end());
      fillElementsToNodesMap(graph, f, eptrIndex, eindIndex, numVertex,
                             f->quadrangles.begin(), f->quadrangles.end());
    }
  }

  // Loop over edges
  if(selectDim < 0 || selectDim == 1){
    for(GModel::const_eiter it = model->firstEdge(); it != model->lastEdge(); ++it){
      const GEdge *e = *it;
      fillElementsToNodesMap(graph, e, eptrIndex, eindIndex, numVertex,
                             e->lines.begin(), e->lines.end());
    }
  }

  // Loop over vertices
  if(selectDim < 0 || selectDim == 0){
    for(GModel::const_viter it = model->firstVertex(); it != model->lastVertex(); ++it){
      GVertex *v = *it;

      fillElementsToNodesMap(graph, v, eptrIndex, eindIndex, numVertex,
                             v->points.begin(), v->points.end());
    }
  }

  // Taking into account the periodicity
  if(isPeriodic(model) && selectDim < 0){
    std::map<int,int> correspondingVertices;
    std::vector<GEntity*> entities;
    model->getEntities(entities);
    for(unsigned int i = 0; i < entities.size(); i++){
      for(std::map<MVertex*,MVertex*>::iterator it =
            entities[i]->correspondingVertices.begin();
          it != entities[i]->correspondingVertices.end(); ++it){
        correspondingVertices.insert(std::pair<int,int>
                                     (graph.vertex(it->first->getNum()-1),
                                      graph.vertex(it->second->getNum()-1)));
      }
    }
    for(int i = 0; i < eindSize; i++){
      std::map<int,int>::iterator it = correspondingVertices.find(graph.eind(i));
      if(it != correspondingVertices.end()){
        graph.eind(i,it->second);
      }
    }
  }

  return 0;
}

static void createDualGraph(Graph &graph, bool connectedAll)
{
  int *nptr = new int[graph.nn()+1];
  for(unsigned int i = 0; i < graph.nn()+1; i++) nptr[i] = 0;
  int *nind = new int[graph.eptr(graph.ne())];
  for(unsigned int i = 0; i < graph.eptr(graph.ne()); i++) nind[i] = 0;

  for(unsigned int i = 0; i < graph.ne(); i++){
    for(unsigned int j = graph.eptr(i); j < graph.eptr(i+1); j++){
      nptr[graph.eind(j)]++;
    }
  }

  for(unsigned int i = 1; i < graph.nn(); i++) nptr[i] += nptr[i-1];
  for(unsigned int i = graph.nn(); i > 0; i--) nptr[i] = nptr[i-1];
  nptr[0] = 0;

  for(unsigned int i = 0; i < graph.ne(); i++){
    for(unsigned int j = graph.eptr(i); j < graph.eptr(i+1); j++){
      nind[nptr[graph.eind(j)]++] = i;
    }
  }

  for(unsigned int i = graph.nn(); i > 0; i--) nptr[i] = nptr[i-1];
  nptr[0] = 0;

  graph.xadjResize(graph.ne()+1);
  int *nbrs = new int[graph.ne()];
  int *marker = new int[graph.ne()];
  for(unsigned int i = 0; i < graph.ne(); i++){
    nbrs[i] = 0;
    marker[i] = 0;
  }

  for(unsigned int i = 0; i < graph.ne(); i++){
    unsigned int l = 0;
    for(unsigned int j = graph.eptr(i); j < graph.eptr(i+1); j++){
      for(int k = nptr[graph.eind(j)]; k < nptr[graph.eind(j)+1]; k++){
        if(nind[k] != (int)i){
          if(marker[nind[k]] == 0) nbrs[l++] = nind[k];
          marker[nind[k]]++;
        }
      }
    }

    unsigned int nbrsNeighbors = 0;
    for(unsigned int j = 0; j < l; j++){
      if(marker[nbrs[j]] >=
         (connectedAll ? 1 :
          graph.element(i)->numCommonNodesInDualGraph(graph.element(nbrs[j]))))
        nbrsNeighbors++;
      marker[nbrs[j]] = 0;
      nbrs[j] = 0;
    }

    graph.xadj(i, nbrsNeighbors);
  }

  for(unsigned int i = 1; i < graph.ne(); i++) graph.xadj(i,graph.xadj(i)+graph.xadj(i-1));
  for(unsigned int i = graph.ne(); i > 0; i--) graph.xadj(i,graph.xadj(i-1));
  graph.xadj(0,0);

  graph.adjncyResize(graph.xadj(graph.ne()));
  for(unsigned int i = 0; i < graph.ne(); i++){
    unsigned int l = 0;
    for(unsigned int j = graph.eptr(i); j < graph.eptr(i+1); j++){
      for(int k = nptr[graph.eind(j)]; k < nptr[graph.eind(j)+1]; k++){
        if(nind[k] != (int)i){
          if (marker[nind[k]] == 0) nbrs[l++] = nind[k];
          marker[nind[k]]++;
        }
      }
    }

    for(unsigned int j = 0; j < l; j++){
      if(marker[nbrs[j]] >=
         (connectedAll ? 1 :
          graph.element(i)->numCommonNodesInDualGraph(graph.element(nbrs[j])))){
        graph.adjncy(graph.xadj(i), nbrs[j]);
        graph.xadj(i, graph.xadj(i)+1);
      }
      marker[nbrs[j]] = 0;
      nbrs[j] = 0;
    }
  }
  delete[] nbrs;
  delete[] marker;

  for(unsigned int i = graph.ne(); i > 0; i--) graph.xadj(i, graph.xadj(i-1));
  graph.xadj(0, 0);

  delete[] nptr;
  delete[] nind;
}

// Partition a graph created by MakeGraph using Metis library. Returns: 0 =
// success, 1 = error, 2 = exception thrown.
static int PartitionGraph(Graph &graph)
{
#ifdef HAVE_METIS
  Msg::Info("Running METIS graph partitioner");

  try {
    int metisOptions[METIS_NOPTIONS];
    METIS_SetDefaultOptions((idx_t *)metisOptions);

    switch(CTX::instance()->mesh.metisAlgorithm){
      case 1: // Recursive
        metisOptions[METIS_OPTION_PTYPE] = METIS_PTYPE_RB;
        break;
      case 2: // K-way
        metisOptions[METIS_OPTION_PTYPE] = METIS_PTYPE_KWAY;
        break;
      default:
        Msg::Info("Unknown partitioning algorithm");
        break;
    }

    switch(CTX::instance()->mesh.metisEdgeMatching){
      case 1: // Random matching
        metisOptions[METIS_OPTION_CTYPE] = METIS_CTYPE_RM;
        break;
      case 2: // Sorted heavy-edge matching
        metisOptions[METIS_OPTION_CTYPE] = METIS_CTYPE_SHEM;
        break;
      default:
        Msg::Info("Unknown partitioning edge matching");
        break;
    }

    switch(CTX::instance()->mesh.metisRefinementAlgorithm){
      case 1: // FM-based cut refinement
        metisOptions[METIS_OPTION_RTYPE] = METIS_RTYPE_FM;
        break;
      case 2: // Greedy boundary refinement
        metisOptions[METIS_OPTION_RTYPE] = METIS_RTYPE_GREEDY;
        break;
      case 3: // Two-sided node FM refinement
        metisOptions[METIS_OPTION_RTYPE] = METIS_RTYPE_SEP2SIDED;
        break;
      case 4: // One-sided node FM refinement
        metisOptions[METIS_OPTION_RTYPE] = METIS_RTYPE_SEP1SIDED;
        break;
      default:
        Msg::Info("Unknown partitioning refinement algorithm");
        break;
    }

    // C numbering
    metisOptions[METIS_OPTION_NUMBERING] = 0;
    // Specifies the type of objective
    metisOptions[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;

    int objval;
    unsigned int *epart = new unsigned int[graph.ne()];
    const unsigned int ne = graph.ne();
    const int numPart = graph.nparts();
    int ncon = 1;

    graph.fillDefaultWeights();

    int metisError = 0;
    createDualGraph(graph, false);

    if (metisOptions[METIS_OPTION_PTYPE] == METIS_PTYPE_KWAY){
      metisError = METIS_PartGraphKway
        ((idx_t *)&ne, (idx_t *)&ncon, (idx_t *)graph.xadj(),
         (idx_t *)graph.adjncy(), (idx_t *)graph.vwgt(),
         (idx_t *)0, 0, (idx_t *)&numPart, 0, 0,
         (idx_t *)metisOptions, (idx_t *)&objval, (idx_t *)epart);
    }
    else{
      metisError = METIS_PartGraphRecursive
        ((idx_t *)&ne, (idx_t *)&ncon, (idx_t *)graph.xadj(),
         (idx_t *)graph.adjncy(), (idx_t *)graph.vwgt(),
         (idx_t *)0, 0, (idx_t *)&numPart, 0, 0,
         (idx_t *)metisOptions, (idx_t *)&objval, (idx_t *)epart);
    }

    switch(metisError){
    case METIS_OK:
      break;
    case METIS_ERROR_INPUT:
      Msg::Error("METIS input error");
      return 1;
    case METIS_ERROR_MEMORY:
      Msg::Error("METIS memoty error");
      return 1;
    case METIS_ERROR:
    default:
      Msg::Error("METIS error");
      return 1;
    }

    // Check and correct the topology
    for(unsigned int i = 0; i < graph.ne(); i++){
      if(graph.element(i)->getDim() == (int)graph.dim()) continue;

      for(unsigned int j = graph.xadj(i); j < graph.xadj(i+1); j++){
        if(graph.element(graph.adjncy(j))->getDim() == graph.element(i)->getDim()+1){
          if(epart[i] != epart[graph.adjncy(j)]){
            epart[i] = epart[graph.adjncy(j)];
            break;
          }
        }

        if(graph.element(graph.adjncy(j))->getDim() == graph.element(i)->getDim()+2){
          if(epart[i] != epart[graph.adjncy(j)]){
            epart[i] = epart[graph.adjncy(j)];
            break;
          }
        }

        if(graph.element(graph.adjncy(j))->getDim() == graph.element(i)->getDim()+3){
          if(epart[i] != epart[graph.adjncy(j)]){
            epart[i] = epart[graph.adjncy(j)];
            break;
          }
        }
      }
    }
    graph.partition(epart);

    Msg::Info("%d partitions, %d total edge-cuts", numPart, objval);
  }
  catch(...) {
    Msg::Error("METIS exception");
    return 2;
  }
#endif

  return 0;
}

template <class ITERATOR>
static void assignElementsToEntities(GModel *const model,
                                     hashmap<MElement*, unsigned int> &elmToPartition,
                                     std::vector<GRegion *> &newRegions,
                                     ITERATOR it_beg, ITERATOR it_end)
{
  for(ITERATOR it = it_beg; it != it_end; ++it){
    const unsigned int partition = elmToPartition[(*it)]-1;

    if(!newRegions[partition]){
      std::vector<unsigned int> partitions;
      partitions.push_back(partition);
      partitionRegion *dr = new partitionRegion
        (model, model->getMaxElementaryNumber(3)+1, partitions);
      model->add(dr);
      newRegions[partition] = dr;
    }

    newRegions[partition]->addElement((*it)->getType(), *it);
  }
}

template <class ITERATOR>
static void assignElementsToEntities(GModel *const model,
                                     hashmap<MElement*, unsigned int> &elmToPartition,
                                     std::vector<GFace *> &newFaces,
                                     ITERATOR it_beg, ITERATOR it_end)
{
  for(ITERATOR it = it_beg; it != it_end; ++it){
    const unsigned int partition = elmToPartition[(*it)]-1;

    if(!newFaces[partition]){
      std::vector<unsigned int> partitions;
      partitions.push_back(partition);
      partitionFace *df = new partitionFace
        (model, model->getMaxElementaryNumber(2)+1, partitions);
      model->add(df);
      newFaces[partition] = df;
    }

    newFaces[partition]->addElement((*it)->getType(), *it);
  }
}

template <class ITERATOR>
static void assignElementsToEntities(GModel *const model,
                                     hashmap<MElement*, unsigned int> &elmToPartition,
                                     std::vector<GEdge *> &newEdges,
                                     ITERATOR it_beg, ITERATOR it_end)
{
  for(ITERATOR it = it_beg; it != it_end; ++it){
    const unsigned int partition = elmToPartition[(*it)]-1;

    if(!newEdges[partition]){
      std::vector<unsigned int> partitions;
      partitions.push_back(partition);
      partitionEdge *de = new partitionEdge
        (model, model->getMaxElementaryNumber(1)+1, 0, 0, partitions);
      model->add(de);
      newEdges[partition] = de;
    }

    newEdges[partition]->addLine(reinterpret_cast<MLine*>(*it));
  }
}

template <class ITERATOR>
static void assignElementsToEntities(GModel *const model,
                                     hashmap<MElement*, unsigned int> &elmToPartition,
                                     std::vector<GVertex *> &newVertices,
                                     ITERATOR it_beg, ITERATOR it_end)
{
  for(ITERATOR it = it_beg; it != it_end; ++it){
    const unsigned int partition = elmToPartition[(*it)]-1;

    if(!newVertices[partition]){
      std::vector<unsigned int> partitions;
      partitions.push_back(partition);
      partitionVertex *dv = new partitionVertex
        (model, model->getMaxElementaryNumber(0)+1, partitions);
      model->add(dv);
      newVertices[partition] = dv;
    }

    newVertices[partition]->addPoint(reinterpret_cast<MPoint*>(*it));
  }
}

template <class ITERATOR>
static void setVerticesToEntity(GEntity *const entity,
                                ITERATOR it_beg, ITERATOR it_end, bool inAllDim)
{
  for(ITERATOR it = it_beg; it != it_end; ++it){
    for(int i = 0; i < (*it)->getNumVertices(); i++){
      if(!(*it)->getVertex(i)->onWhat()){
        (*it)->getVertex(i)->setEntity(entity);
        entity->addMeshVertex((*it)->getVertex(i));
      }
      else{
        if(inAllDim) entity->addMeshVertex((*it)->getVertex(i));
      }
    }
  }
}

// Assign the vertices to its corresponding entity
static void AssignMeshVertices(GModel *model, int dim = -1, bool inAllDim = false)
{
  // Loop over vertices
  if(dim == 0 || dim == -1){
    for(GModel::const_viter it = model->firstVertex(); it != model->lastVertex(); ++it){
      for(unsigned int i = 0; i < (*it)->getNumMeshElements(); i++){
        for(int j = 0; j < (*it)->getMeshElement(i)->getNumVertices(); j++){
          (*it)->getMeshElement(i)->getVertex(j)->setEntity(0);
        }
      }
      (*it)->mesh_vertices.clear();
    }
  }

  // Loop over edges
  if(dim == 1 || dim == -1){
    for(GModel::const_eiter it = model->firstEdge(); it != model->lastEdge(); ++it){
      for(unsigned int i = 0; i < (*it)->getNumMeshElements(); i++){
        for(int j = 0; j < (*it)->getMeshElement(i)->getNumVertices(); j++){
          (*it)->getMeshElement(i)->getVertex(j)->setEntity(0);
        }
      }
      (*it)->mesh_vertices.clear();
    }
  }

  // Loop over faces
  if(dim == 2 || dim == -1){
    for(GModel::const_fiter it = model->firstFace(); it != model->lastFace(); ++it){
      for(unsigned int i = 0; i < (*it)->getNumMeshElements(); i++){
        for(int j = 0; j < (*it)->getMeshElement(i)->getNumVertices(); j++){
          (*it)->getMeshElement(i)->getVertex(j)->setEntity(0);
        }
      }
      (*it)->mesh_vertices.clear();
    }
  }

  // Loop over regions
  if(dim == 3 || dim == -1){
    for(GModel::const_riter it = model->firstRegion(); it != model->lastRegion(); ++it){
      for(unsigned int i = 0; i < (*it)->getNumMeshElements(); i++){
        for(int j = 0; j < (*it)->getMeshElement(i)->getNumVertices(); j++){
          (*it)->getMeshElement(i)->getVertex(j)->setEntity(0);
        }
      }
      (*it)->mesh_vertices.clear();
    }
  }

  // Loop over vertices
  if(dim == 0 || dim == -1){
    for(GModel::const_viter it = model->firstVertex(); it != model->lastVertex(); ++it){
      setVerticesToEntity(*it, (*it)->points.begin(), (*it)->points.end(), inAllDim);
    }
  }

  // Loop over edges
  if(dim == 1 || dim == -1){
    for(GModel::const_eiter it = model->firstEdge(); it != model->lastEdge(); ++it){
        setVerticesToEntity(*it, (*it)->lines.begin(), (*it)->lines.end(), inAllDim);
    }
  }

  // Loop over faces
  if(dim == 2 || dim == -1){
    for(GModel::const_fiter it = model->firstFace(); it != model->lastFace(); ++it){
        setVerticesToEntity(*it, (*it)->triangles.begin(), (*it)->triangles.end(), inAllDim);
        setVerticesToEntity(*it, (*it)->quadrangles.begin(), (*it)->quadrangles.end(), inAllDim);
    }
  }

  // Loop over regions
  if(dim == 3 || dim == -1){
    for(GModel::const_riter it = model->firstRegion(); it != model->lastRegion(); ++it){
        setVerticesToEntity(*it, (*it)->tetrahedra.begin(), (*it)->tetrahedra.end(), inAllDim);
        setVerticesToEntity(*it, (*it)->hexahedra.begin(), (*it)->hexahedra.end(), inAllDim);
        setVerticesToEntity(*it, (*it)->prisms.begin(), (*it)->prisms.end(), inAllDim);
        setVerticesToEntity(*it, (*it)->pyramids.begin(), (*it)->pyramids.end(), inAllDim);
        setVerticesToEntity(*it, (*it)->trihedra.begin(), (*it)->trihedra.end(), inAllDim);
    }
  }
}

static void AssignMeshVerticesToEntity(GEntity *entity, bool inAllDim = false)
{
  for(unsigned int i = 0; i < entity->getNumMeshElements(); i++){
    for(int j = 0; j < entity->getMeshElement(i)->getNumVertices(); j++){
      entity->getMeshElement(i)->getVertex(j)->setEntity(0);
    }
  }
  entity->mesh_vertices.clear();

  switch (entity->dim()) {
    case 0:
      setVerticesToEntity(entity, static_cast<GVertex*>(entity)->points.begin(),
                          static_cast<GVertex*>(entity)->points.end(), inAllDim);
      break;
    case 1:
      setVerticesToEntity(entity, static_cast<GEdge*>(entity)->lines.begin(),
                          static_cast<GEdge*>(entity)->lines.end(), inAllDim);
      break;
    case 2:
      setVerticesToEntity(entity, static_cast<GFace*>(entity)->triangles.begin(),
                          static_cast<GFace*>(entity)->triangles.end(), inAllDim);
      setVerticesToEntity(entity, static_cast<GFace*>(entity)->quadrangles.begin(),
                          static_cast<GFace*>(entity)->quadrangles.end(), inAllDim);
      break;
    case 3:
      setVerticesToEntity(entity, static_cast<GRegion*>(entity)->tetrahedra.begin(),
                          static_cast<GRegion*>(entity)->tetrahedra.end(), inAllDim);
      setVerticesToEntity(entity, static_cast<GRegion*>(entity)->hexahedra.begin(),
                          static_cast<GRegion*>(entity)->hexahedra.end(), inAllDim);
      setVerticesToEntity(entity, static_cast<GRegion*>(entity)->prisms.begin(),
                          static_cast<GRegion*>(entity)->prisms.end(), inAllDim);
      setVerticesToEntity(entity, static_cast<GRegion*>(entity)->pyramids.begin(),
                          static_cast<GRegion*>(entity)->pyramids.end(), inAllDim);
      setVerticesToEntity(entity, static_cast<GRegion*>(entity)->trihedra.begin(),
                          static_cast<GRegion*>(entity)->trihedra.end(), inAllDim);
      break;
    default:
      break;
  }
}

static void fillConnectedElements(std::vector< std::set<MElement*> > &connectedElements,
                                  Graph &graph)
{
  std::stack<unsigned int> elementStack;
  std::set<MElement*> elements;
  int startElement = 0;
  bool stop = true;

  do {
    //Inititalization
    elementStack.push(startElement);
    elements.insert(graph.element(startElement));

    while(elementStack.size() != 0){
      unsigned int top = elementStack.top();
      elementStack.pop();

      for(unsigned int i = graph.xadj(top); i < graph.xadj(top+1); i++){
        if(graph.adjncy(i) != 0){
          elementStack.push(graph.adjncy(i));
          elements.insert(graph.element(graph.adjncy(i)));
          graph.adjncy(i,0);
        }
      }
    }
    connectedElements.push_back(elements);
    elements.clear();

    stop = true;
    for(unsigned int i = 0; i < graph.ne(); i++){
      for(unsigned int j = graph.xadj(i); j < graph.xadj(i+1); j++){
        if(graph.adjncy(j) != 0){
          startElement = i;
          stop = false;
          break;
        }
      }
      if(!stop) break;
    }
    break;
  } while(!stop);
}

// Create the new volume entities (omega)
static void CreateNewEntities(GModel *const model,
                              hashmap<MElement*, unsigned int> &elmToPartition)
{
  std::set<GRegion*, GEntityLessThan> regions = model->getRegions();
  std::set<GFace*, GEntityLessThan> faces = model->getFaces();
  std::set<GEdge*, GEntityLessThan> edges = model->getEdges();
  std::set<GVertex*, GEntityLessThan> vertices = model->getVertices();

  for(GModel::const_riter it = regions.begin(); it != regions.end(); ++it){
    std::vector<GRegion *> newRegions(model->getNumPartitions(), 0);

    assignElementsToEntities(model, elmToPartition, newRegions,
                             (*it)->tetrahedra.begin(), (*it)->tetrahedra.end());
    assignElementsToEntities(model, elmToPartition, newRegions,
                             (*it)->hexahedra.begin(), (*it)->hexahedra.end());
    assignElementsToEntities(model, elmToPartition, newRegions,
                             (*it)->prisms.begin(), (*it)->prisms.end());
    assignElementsToEntities(model, elmToPartition, newRegions,
                             (*it)->pyramids.begin(), (*it)->pyramids.end());
    assignElementsToEntities(model, elmToPartition, newRegions,
                             (*it)->trihedra.begin(), (*it)->trihedra.end());

    for(unsigned int i = 0; i < model->getNumPartitions(); i++){
      if(newRegions[i]){
        static_cast<partitionRegion*>(newRegions[i])->setParentEntity(*it);
        std::vector<int> physicalEntities = (*it)->getPhysicalEntities();
        for(unsigned int j = 0; j < physicalEntities.size(); j++){
          newRegions[i]->addPhysicalEntity(physicalEntities[j]);
        }
      }
    }

    (*it)->mesh_vertices.clear();

    (*it)->tetrahedra.clear();
    (*it)->hexahedra.clear();
    (*it)->prisms.clear();
    (*it)->pyramids.clear();
    (*it)->trihedra.clear();
  }

  for(GModel::const_fiter it = faces.begin(); it != faces.end(); ++it){
    std::vector<GFace *> newFaces(model->getNumPartitions(), 0);

    assignElementsToEntities(model, elmToPartition, newFaces,
                             (*it)->triangles.begin(), (*it)->triangles.end());
    assignElementsToEntities(model, elmToPartition, newFaces,
                             (*it)->quadrangles.begin(), (*it)->quadrangles.end());

    for(unsigned int i = 0; i < model->getNumPartitions(); i++){
      if(newFaces[i]){
        static_cast<partitionFace*>(newFaces[i])->setParentEntity(*it);
        std::vector<int> physicalEntities = (*it)->getPhysicalEntities();
        for(unsigned int j = 0; j < physicalEntities.size(); j++){
          newFaces[i]->addPhysicalEntity(physicalEntities[j]);
        }
      }
    }

    (*it)->mesh_vertices.clear();

    (*it)->triangles.clear();
    (*it)->quadrangles.clear();
  }

  for(GModel::const_eiter it = edges.begin(); it != edges.end(); ++it){
    std::vector<GEdge *> newEdges(model->getNumPartitions(), 0);

    assignElementsToEntities(model, elmToPartition, newEdges,
                             (*it)->lines.begin(), (*it)->lines.end());

    for(unsigned int i = 0; i < model->getNumPartitions(); i++){
      if(newEdges[i]){
        static_cast<partitionEdge*>(newEdges[i])->setParentEntity(*it);
        std::vector<int> physicalEntities = (*it)->getPhysicalEntities();
        for(unsigned int j = 0; j < physicalEntities.size(); j++){
          newEdges[i]->addPhysicalEntity(physicalEntities[j]);
        }
      }
    }

    (*it)->mesh_vertices.clear();

    (*it)->lines.clear();
  }

  for(GModel::const_viter it = vertices.begin(); it != vertices.end(); ++it){
    std::vector<GVertex *> newVertices(model->getNumPartitions(), 0);

    assignElementsToEntities(model, elmToPartition, newVertices,
                             (*it)->points.begin(), (*it)->points.end());

    for(unsigned int i = 0; i < model->getNumPartitions(); i++){
      if(newVertices[i]){
        static_cast<partitionVertex*>(newVertices[i])->setParentEntity((*it));
        std::vector<int> physicalEntities = (*it)->getPhysicalEntities();
        for(unsigned int j = 0; j < physicalEntities.size(); j++){
          newVertices[i]->addPhysicalEntity(physicalEntities[j]);
        }
      }
    }

    (*it)->mesh_vertices.clear();

    (*it)->points.clear();
  }

  // If we don't create the partition topology let's just assume that the user
  // does not care about multi-connected partitions or partition boundaries.
  if(!CTX::instance()->mesh.partitionCreateTopology) return;

  // Divide the non connected entities
  regions = model->getRegions();
  faces = model->getFaces();
  edges = model->getEdges();
  vertices = model->getVertices();

  // Loop over vertices
  for(GModel::const_viter it = vertices.begin(); it != vertices.end(); ++it){
    if((*it)->geomType() == GEntity::PartitionVertex){
      partitionVertex* vertex = static_cast<partitionVertex*>(*it);

      if(vertex->getNumMeshElements() > 1){
        for(unsigned int i = 0; i < vertex->getNumMeshElements(); i++){
          // Create the new partitionVertex
          partitionVertex *pvertex = new partitionVertex
            (model, model->getMaxElementaryNumber(0)+1, vertex->getPartitions());
          // Assign physicals and parent entity
          std::vector<int> physicalTags = vertex->getPhysicalEntities();
          for(unsigned int j = 0; j < physicalTags.size(); j++){
            pvertex->addPhysicalEntity(physicalTags[j]);
          }
          pvertex->setParentEntity(vertex->getParentEntity());
          // Add to model
          model->add(pvertex);
          // Add elements
          pvertex->addElement(vertex->getMeshElement(i)->getType(),
                              vertex->getMeshElement(i));
        }

        model->remove(vertex);
        vertex->points.clear();
        vertex->mesh_vertices.clear();
        delete vertex;
      }
    }
  }

  // Loop over edges
  for(GModel::const_eiter it = edges.begin(); it != edges.end(); ++it){
    if((*it)->geomType() == GEntity::PartitionCurve){
      partitionEdge* edge = static_cast<partitionEdge*>(*it);
      AssignMeshVerticesToEntity(edge, false);

      // We build a graph
      Graph graph(model);
      graph.ne(edge->getNumMeshElements());
      graph.nn(edge->getNumMeshVertices());
      graph.dim(1);
      graph.elementResize(graph.ne());
      graph.vertexResize(model->getMaxVertexNumber());
      graph.eptrResize(graph.ne()+1);
      graph.eptr(0,0);
      const int eindSize = getSizeOfEind(edge);
      graph.eindResize(eindSize);

      int eptrIndex = 0;
      int eindIndex = 0;
      int numVertex = 0;

      fillElementsToNodesMap(graph, edge, eptrIndex, eindIndex, numVertex,
                             edge->lines.begin(), edge->lines.end());
      createDualGraph(graph, false);

      std::vector< std::set<MElement*> > connectedElements;
      fillConnectedElements(connectedElements, graph);
      graph.clear();

      if(connectedElements.size() > 1){
        for(unsigned int i = 0; i < connectedElements.size(); i++){
          // Create the new partitionEdge
          partitionEdge *pedge = new partitionEdge
            (model, model->getMaxElementaryNumber(1)+1, 0, 0, edge->getPartitions());
          // Assign physicals and parent entity
          std::vector<int> physicalTags = edge->getPhysicalEntities();
          for(unsigned int j = 0; j < physicalTags.size(); j++){
            pedge->addPhysicalEntity(physicalTags[j]);
          }
          pedge->setParentEntity(edge->getParentEntity());
          // Add to model
          model->add(pedge);
          for(std::set<MElement*>::iterator itSet = connectedElements[i].begin();
              itSet != connectedElements[i].end(); ++itSet){
            // Add elements
            pedge->addElement((*itSet)->getType(), (*itSet));
          }
        }

        model->remove(edge);
        edge->lines.clear();
        edge->mesh_vertices.clear();
        delete edge;
      }

      connectedElements.clear();
    }
  }

  // Loop over faces
  for(GModel::const_fiter it = faces.begin(); it != faces.end(); ++it){
    if((*it)->geomType() == GEntity::PartitionSurface){
      partitionFace* face = static_cast<partitionFace*>(*it);
      AssignMeshVerticesToEntity(face, false);

      // We build a graph
      Graph graph(model);
      graph.ne(face->getNumMeshElements());
      graph.nn(face->getNumMeshVertices());
      graph.dim(1);
      graph.elementResize(graph.ne());
      graph.vertexResize(model->getMaxVertexNumber());
      graph.eptrResize(graph.ne()+1);
      graph.eptr(0,0);
      const int eindSize = getSizeOfEind(face);
      graph.eindResize(eindSize);

      int eptrIndex = 0;
      int eindIndex = 0;
      int numVertex = 0;

      fillElementsToNodesMap(graph, face, eptrIndex, eindIndex, numVertex,
                             face->triangles.begin(), face->triangles.end());
      fillElementsToNodesMap(graph, face, eptrIndex, eindIndex, numVertex,
                             face->quadrangles.begin(), face->quadrangles.end());
      createDualGraph(graph, false);

      std::vector< std::set<MElement*> > connectedElements;
      fillConnectedElements(connectedElements, graph);
      graph.clear();

      if(connectedElements.size() > 1){
        for(unsigned int i = 0; i < connectedElements.size(); i++){
          // Create the new partitionFace
          partitionFace *pface = new partitionFace
            (model, model->getMaxElementaryNumber(2)+1, face->getPartitions());
          // Assign physicals and parent entity
          std::vector<int> physicalTags = face->getPhysicalEntities();
          for(unsigned int j = 0; j < physicalTags.size(); j++){
            pface->addPhysicalEntity(physicalTags[j]);
          }
          pface->setParentEntity(face->getParentEntity());
          // Add to model
          model->add(pface);
          for(std::set<MElement*>::iterator itSet = connectedElements[i].begin();
              itSet != connectedElements[i].end(); ++itSet){
            // Add elements
            pface->addElement((*itSet)->getType(), (*itSet));
          }
        }

        model->remove(face);
        face->triangles.clear();
        face->quadrangles.clear();
        face->mesh_vertices.clear();
        delete face;
      }

      connectedElements.clear();
    }
  }

  // Loop over regions
  for(GModel::const_riter it = regions.begin(); it != regions.end(); ++it){
    if((*it)->geomType() == GEntity::PartitionVolume){
      partitionRegion* region = static_cast<partitionRegion*>(*it);
      AssignMeshVerticesToEntity(region, false);

      //We build a graph
      Graph graph(model);
      graph.ne(region->getNumMeshElements());
      graph.nn(region->getNumMeshVertices());
      graph.dim(1);
      graph.elementResize(graph.ne());
      graph.vertexResize(model->getMaxVertexNumber());
      graph.eptrResize(graph.ne()+1);
      graph.eptr(0,0);
      const int eindSize = getSizeOfEind(region);
      graph.eindResize(eindSize);

      int eptrIndex = 0;
      int eindIndex = 0;
      int numVertex = 0;

      fillElementsToNodesMap(graph, region, eptrIndex, eindIndex, numVertex,
                             region->tetrahedra.begin(), region->tetrahedra.end());
      fillElementsToNodesMap(graph, region, eptrIndex, eindIndex, numVertex,
                             region->hexahedra.begin(), region->hexahedra.end());
      fillElementsToNodesMap(graph, region, eptrIndex, eindIndex, numVertex,
                             region->prisms.begin(), region->prisms.end());
      fillElementsToNodesMap(graph, region, eptrIndex, eindIndex, numVertex,
                             region->pyramids.begin(), region->pyramids.end());
      fillElementsToNodesMap(graph, region, eptrIndex, eindIndex, numVertex,
                             region->trihedra.begin(), region->trihedra.end());
      createDualGraph(graph, false);

      std::vector< std::set<MElement*> > connectedElements;
      fillConnectedElements(connectedElements, graph);
      graph.clear();

      if(connectedElements.size() > 1){
        for(unsigned int i = 0; i < connectedElements.size(); i++){
          // Create the new partitionRegion
          partitionRegion *pregion = new partitionRegion
            (model, model->getMaxElementaryNumber(3)+1, region->getPartitions());
          // Assign physicals and parent entity
          std::vector<int> physicalTags = (*it)->getPhysicalEntities();
          for(unsigned int j = 0; j < physicalTags.size(); j++){
            pregion->addPhysicalEntity(physicalTags[j]);
          }
          pregion->setParentEntity(region->getParentEntity());
          // Add to model
          model->add(pregion);
          for(std::set<MElement*>::iterator itSet = connectedElements[i].begin();
              itSet != connectedElements[i].end(); ++itSet){
            //Add elements
            pregion->addElement((*itSet)->getType(), (*itSet));
          }
        }

        model->remove(region);
        region->tetrahedra.clear();
        region->hexahedra.clear();
        region->prisms.clear();
        region->pyramids.clear();
        region->trihedra.clear();
        region->mesh_vertices.clear();
        delete region;
      }

      connectedElements.clear();
    }
  }
}

static void fillElementToEntity(GModel *const model,
                                std::map<MElement*, GEntity*> &elmToEntity)
{
  // Loop over regions
  for(GModel::const_riter it = model->firstRegion(); it != model->lastRegion(); ++it){
    for(std::vector<MTetrahedron*>::iterator itElm = (*it)->tetrahedra.begin();
        itElm != (*it)->tetrahedra.end(); ++itElm)
      elmToEntity.insert(std::pair<MElement*, GEntity*>(*itElm, *it));
    for(std::vector<MHexahedron*>::iterator itElm = (*it)->hexahedra.begin();
        itElm != (*it)->hexahedra.end(); ++itElm)
      elmToEntity.insert(std::pair<MElement*, GEntity*>(*itElm, *it));
    for(std::vector<MPrism*>::iterator itElm = (*it)->prisms.begin();
        itElm != (*it)->prisms.end(); ++itElm)
      elmToEntity.insert(std::pair<MElement*, GEntity*>(*itElm, *it));
    for(std::vector<MPyramid*>::iterator itElm = (*it)->pyramids.begin();
        itElm != (*it)->pyramids.end(); ++itElm)
      elmToEntity.insert(std::pair<MElement*, GEntity*>(*itElm, *it));
    for(std::vector<MTrihedron*>::iterator itElm = (*it)->trihedra.begin();
        itElm != (*it)->trihedra.end(); ++itElm)
      elmToEntity.insert(std::pair<MElement*, GEntity*>(*itElm, *it));
  }

  // Loop over faces
  for(GModel::const_fiter it = model->firstFace(); it != model->lastFace(); ++it){
    for(std::vector<MTriangle*>::iterator itElm = (*it)->triangles.begin();
        itElm != (*it)->triangles.end(); ++itElm)
      elmToEntity.insert(std::pair<MElement*, GEntity*>(*itElm, *it));
    for(std::vector<MQuadrangle*>::iterator itElm = (*it)->quadrangles.begin();
        itElm != (*it)->quadrangles.end(); ++itElm)
      elmToEntity.insert(std::pair<MElement*, GEntity*>(*itElm, *it));
  }

  // Loop over edges
  for(GModel::const_eiter it = model->firstEdge(); it != model->lastEdge(); ++it){
    for(std::vector<MLine*>::iterator itElm = (*it)->lines.begin();
        itElm != (*it)->lines.end(); ++itElm)
      elmToEntity.insert(std::pair<MElement*, GEntity*>(*itElm, *it));
  }

  // Loop over vertices
  for(GModel::const_viter it = model->firstVertex(); it != model->lastVertex(); ++it){
    for(std::vector<MPoint*>::iterator itElm = (*it)->points.begin();
        itElm != (*it)->points.end(); ++itElm)
      elmToEntity.insert(std::pair<MElement*, GEntity*>(*itElm, *it));
  }
}

static MElement* getReferenceElement(const std::vector< std::pair<MElement*,
                                     std::vector<unsigned int> > > &elementPairs)
{
  unsigned int min = std::numeric_limits<unsigned int>::max();
  std::vector< std::pair<MElement*, std::vector<unsigned int> > > minSizeElementPairs;
  std::vector< std::pair<MElement*, std::vector<unsigned int> > > minSizeElementPairsTmp;

  // Take only the elements having the less partition in commun. For exemple we
  // take (1,2) and (3,8) but not (2,5,9) or (1,4,5,7)
  for(unsigned int i = 0; i < elementPairs.size(); i++){
    if(min > elementPairs[i].second.size()){
      minSizeElementPairs.clear();
      min = elementPairs[i].second.size();
      minSizeElementPairs.push_back(elementPairs[i]);
    }
    else if(min == elementPairs[i].second.size()){
      minSizeElementPairs.push_back(elementPairs[i]);
    }
  }

  // Check if the element separated different partitions
  if(minSizeElementPairs.size() == elementPairs.size()){
    bool isEqual = true;
    for(unsigned int i = 1; i < minSizeElementPairs.size(); i++){
      if(minSizeElementPairs[i].second != minSizeElementPairs[0].second){
        isEqual = false;
        break;
      }
    }
    if(isEqual) return 0;
  }

  while(minSizeElementPairs.size() > 1){
    min = std::numeric_limits<unsigned int>::max();
    for(unsigned int i = 0; i < minSizeElementPairs.size(); i++){
      // The partition vector is sorted thus we can check only the first element
      if(minSizeElementPairs[i].second.size() == 0) return minSizeElementPairs[0].first;
      if(min > minSizeElementPairs[i].second[0]){
        min = minSizeElementPairs[i].second[0];
      }
    }

    for(unsigned int i = 0; i < minSizeElementPairs.size(); i++){
      if(min == minSizeElementPairs[i].second[0]){
        minSizeElementPairs[i].second.erase(minSizeElementPairs[i].second.begin());
        minSizeElementPairsTmp.push_back(minSizeElementPairs[i]);
      }
    }
    minSizeElementPairs.clear();
#if __cplusplus >= 201103L
    minSizeElementPairs = std::move(minSizeElementPairsTmp);
#else
    minSizeElementPairs = minSizeElementPairsTmp;
#endif
    minSizeElementPairsTmp.clear();
  }

  return minSizeElementPairs[0].first;
}

static void getPartitionInVector(std::vector<unsigned int> &partitions,
                                 const std::vector< std::pair<MElement*,
                                 std::vector<unsigned int> > > &boundaryPair)
{
  for(unsigned int i = 0; i < boundaryPair.size(); i++){
    for(unsigned int j = 0; j < boundaryPair[i].second.size(); j++){
      if(std::find(partitions.begin(), partitions.end(), boundaryPair[i].second[j]) ==
         partitions.end()){
        partitions.push_back(boundaryPair[i].second[j]);
      }
    }
  }

  std::sort(partitions.begin(), partitions.end());
}

static void assignPartitionBoundary(GModel *const model, MFace &me, MElement* reference,
                                    std::vector<unsigned int> partitions,
                                    std::multimap<partitionFace*, GEntity*,
                                    Less_partitionFace> &pfaces,
                                    std::map<MElement*, GEntity*> &elementToEntity)
{
  partitionFace pf(model, 1, partitions);
  std::pair< std::multimap<partitionFace*, GEntity*, Less_partitionFace>::iterator,
             std::multimap<partitionFace*, GEntity*, Less_partitionFace>::iterator>
    ret = pfaces.equal_range(&pf);

  partitionFace *ppf = 0;
  // Create the new partition entity for the mesh
  if(ret.first == ret.second){
    // Create new entity and add them to the model
    ppf = new partitionFace(model, model->getMaxElementaryNumber(2)+1, partitions);
    pfaces.insert(std::pair<partitionFace*, GEntity*>(ppf, elementToEntity[reference]));
    model->add(ppf);
  }
  else{
    for(std::multimap<partitionFace*, GEntity*, Less_partitionFace>::iterator it = ret.first;
        it != ret.second; ++it){
      if(elementToEntity[reference] == (*it).second){
        ppf = (*it).first;
      }
    }
    if(!ppf){
      // Create new entity and add them to the model
      ppf = new partitionFace(model, model->getMaxElementaryNumber(2)+1, partitions);
      pfaces.insert(std::pair<partitionFace*, GEntity*>(ppf, elementToEntity[reference]));
      model->add(ppf);
    }
  }

  int numFace = 0;
  for(int i = 0; i < reference->getNumFaces(); i++){
    if(reference->getFace(i) == me){
      numFace = i;
      break;
    }
  }

  if(me.getNumVertices() == 3){
    std::vector<MVertex*> verts;
    reference->getFaceVertices(numFace, verts);

    if(verts.size() == 3){
      MTriangle *element = new MTriangle(verts);
      static_cast<GFace*>(ppf)->addTriangle(element);
      elementToEntity.insert(std::pair<MElement*, GEntity*>(element, ppf));
    }
    else if(verts.size() == 6){
      MTriangle6 *element = new MTriangle6(verts);
      static_cast<GFace*>(ppf)->addTriangle(element);
      elementToEntity.insert(std::pair<MElement*, GEntity*>(element, ppf));
    }
    else{
      MTriangleN *element = new MTriangleN(verts, verts[0]->getPolynomialOrder());
      static_cast<GFace*>(ppf)->addTriangle(element);
      elementToEntity.insert(std::pair<MElement*, GEntity*>(element, ppf));
    }

    for(unsigned int i = 0; i < verts.size(); i++) ppf->addMeshVertex(verts[i]);
  }
  else if(me.getNumVertices() == 4){
    std::vector<MVertex*> verts;
    reference->getFaceVertices(numFace, verts);

    if(verts.size() == 4){
      MQuadrangle *element = new MQuadrangle(verts);
      ppf->addQuadrangle(element);
      elementToEntity.insert(std::pair<MElement*, GEntity*>(element, ppf));
    }
    else if(verts.size() == 8){
      MQuadrangle8 *element = new MQuadrangle8(verts);
      ppf->addQuadrangle(element);
      elementToEntity.insert(std::pair<MElement*, GEntity*>(element, ppf));
    }
    else if(verts.size() == 9){
      MQuadrangle9 *element = new MQuadrangle9(verts);
      ppf->addQuadrangle(element);
      elementToEntity.insert(std::pair<MElement*, GEntity*>(element, ppf));
    }
    else{
      MQuadrangleN *element = new MQuadrangleN(verts, verts[0]->getPolynomialOrder());
      ppf->addQuadrangle(element);
      elementToEntity.insert(std::pair<MElement*, GEntity*>(element, ppf));
    }

    for(unsigned int i = 0; i < verts.size(); i++) ppf->addMeshVertex(verts[i]);
  }
}

static void assignPartitionBoundary(GModel *const model, MEdge &me, MElement* reference,
                                    std::vector<unsigned int> partitions,
                                    std::multimap<partitionEdge*, GEntity*,
                                    Less_partitionEdge> &pedges,
                                    std::map<MElement*, GEntity*> &elementToEntity)
{
  partitionEdge pe(model, 1, 0, 0, partitions);
  std::pair< std::multimap<partitionEdge*, GEntity*, Less_partitionEdge>::iterator,
             std::multimap<partitionEdge*, GEntity*, Less_partitionEdge>::iterator>
    ret = pedges.equal_range(&pe);

  partitionEdge *ppe = 0;
  // Create the new partition entity for the mesh
  if(ret.first == ret.second){
    // Create new entity and add them to the model
    ppe = new partitionEdge(model, model->getMaxElementaryNumber(1)+1, 0, 0, partitions);
    pedges.insert(std::pair<partitionEdge*, GEntity*>(ppe, elementToEntity[reference]));
    model->add(ppe);
  }
  else{
    for(std::multimap<partitionEdge*, GEntity*, Less_partitionEdge>::iterator it = ret.first;
        it != ret.second; ++it){
      if(elementToEntity[reference] == (*it).second){
        ppe = (*it).first;
      }
    }
    if(!ppe){
      // Create new entity and add them to the model
      ppe = new partitionEdge(model, model->getMaxElementaryNumber(1)+1, 0, 0, partitions);
      pedges.insert(std::pair<partitionEdge*, GEntity*>(ppe, elementToEntity[reference]));
      model->add(ppe);
    }
  }

  int numEdge = 0;
  for(int i = 0; i < reference->getNumEdges(); i++){
    if(reference->getEdge(i) == me){
      numEdge = i;
      break;
    }
  }

  if(me.getNumVertices() == 2){
    std::vector<MVertex*> verts;
    reference->getEdgeVertices(numEdge, verts);

    if(verts.size() == 2){
      MLine *element = new MLine(verts);
      ppe->addLine(element);
      elementToEntity.insert(std::pair<MElement*, GEntity*>(element, ppe));
    }
    else if(verts.size() == 3){
      MLine3 *element = new MLine3(verts);
      ppe->addLine(element);
      elementToEntity.insert(std::pair<MElement*, GEntity*>(element, ppe));
    }
    else{
      MLineN *element = new MLineN(verts);
      ppe->addLine(element);
      elementToEntity.insert(std::pair<MElement*, GEntity*>(element, ppe));
    }

    for(unsigned int i = 0; i < verts.size(); i++) ppe->addMeshVertex(verts[i]);
  }
}

static void assignPartitionBoundary(GModel *const model, MVertex *ve, MElement* reference,
                                    std::vector<unsigned int> partitions,
                                    std::multimap<partitionVertex*, GEntity*,
                                    Less_partitionVertex> &pvertices,
                                    std::map<MElement*, GEntity*> &elementToEntity)
{
  partitionVertex pv(model, 1, partitions);
  std::pair< std::multimap<partitionVertex*, GEntity*, Less_partitionVertex>::iterator,
             std::multimap<partitionVertex*, GEntity*, Less_partitionVertex>::iterator >
    ret = pvertices.equal_range(&pv);

  partitionVertex *ppv = 0;
  // Create the new partition entity for the mesh
  if(ret.first == ret.second){
    ppv = new partitionVertex(model, model->getMaxElementaryNumber(0)+1, partitions);
    pvertices.insert(std::pair<partitionVertex*, GEntity*>(ppv, elementToEntity[reference]));
    model->add(ppv);
  }
  else{
    for(std::multimap<partitionVertex*, GEntity*, Less_partitionVertex>::iterator it =
          ret.first; it != ret.second; ++it){
      if(elementToEntity[reference] == (*it).second){
        ppv = (*it).first;
      }
    }
    if(!ppv){
      // Create new entity and add them to the model
      ppv = new partitionVertex(model, model->getMaxElementaryNumber(0)+1, partitions);
      pvertices.insert(std::pair<partitionVertex*, GEntity*>
                       (ppv, elementToEntity[reference]));
      model->add(ppv);
    }
  }

  ppv->addPoint(new MPoint(ve));
  ppv->addMeshVertex(ve);
}

// Create the new entities between each partitions (sigma and bndSigma).
static void CreatePartitionBoundaries(GModel *const model,
                                      const std::vector< std::set<MElement*> >
                                      &boundaryElements)
{
  const int meshDim = model->getDim();
  std::map<MElement*, GEntity*> elementToEntity;
  fillElementToEntity(model, elementToEntity);

  std::multimap<partitionFace*, GEntity*, Less_partitionFace> pfaces;
  std::multimap<partitionEdge*, GEntity*, Less_partitionEdge> pedges;
  std::multimap<partitionVertex*, GEntity*, Less_partitionVertex> pvertices;

  hashmapface faceToElement;
  hashmapedge edgeToElement;
  hashmapvertex vertexToElement;

  if (meshDim >= 3){ // Create partition faces
    Msg::Info(" - Creating partition faces");

    for(unsigned int i = 0; i < model->getNumPartitions(); i++){
      for(std::set<MElement*>::iterator it = boundaryElements[i].begin();
          it != boundaryElements[i].end(); ++it){
        for(int j = 0; j < (*it)->getNumFaces(); j++){
          faceToElement[(*it)->getFace(j)].push_back
          (std::pair<MElement*, std::vector<unsigned int> >
           (*it, std::vector<unsigned int>(1,i)));
        }
      }
    }

    for(hashmapface::const_iterator it = faceToElement.begin();
        it != faceToElement.end(); ++it){
      MFace f = it->first;

      std::vector<unsigned int> partitions;
      getPartitionInVector(partitions, it->second);
      if(partitions.size() < 2) continue;

      MElement* reference = getReferenceElement(it->second);
      if(!reference) continue;

      assignPartitionBoundary(model, f, reference, partitions, pfaces, elementToEntity);
    }
    faceToElement.clear();

    // Divide the non connected entities
    std::set<GFace*, GEntityLessThan> faces = model->getFaces();
    for(GModel::const_fiter it = faces.begin(); it != faces.end(); ++it){
      if((*it)->geomType() == GEntity::PartitionSurface){
        partitionFace* face = static_cast<partitionFace*>(*it);
        AssignMeshVerticesToEntity(face, false);

        // We build a graph
        Graph graph(model);
        graph.ne(face->getNumMeshElements());
        graph.nn(face->getNumMeshVertices());
        graph.dim(1);
        graph.elementResize(graph.ne());
        graph.vertexResize(model->getMaxVertexNumber());
        graph.eptrResize(graph.ne()+1);
        graph.eptr(0,0);
        const int eindSize = getSizeOfEind(face);
        graph.eindResize(eindSize);

        int eptrIndex = 0;
        int eindIndex = 0;
        int numVertex = 0;

        fillElementsToNodesMap(graph, face, eptrIndex, eindIndex, numVertex,
                               face->triangles.begin(), face->triangles.end());
        fillElementsToNodesMap(graph, face, eptrIndex, eindIndex, numVertex,
                               face->quadrangles.begin(), face->quadrangles.end());
        createDualGraph(graph, false);

        std::vector< std::set<MElement*> > connectedElements;
        fillConnectedElements(connectedElements, graph);
        graph.clear();

        if(connectedElements.size() > 1){
          for(unsigned int i = 0; i < connectedElements.size(); i++){
            // Create the new partitionFace
            partitionFace *pface = new partitionFace
              (model, model->getMaxElementaryNumber(2)+1, face->getPartitions());
            // Assign physicals and parent entity
            std::vector<int> physicalTags = face->getPhysicalEntities();
            for(unsigned int j = 0; j < physicalTags.size(); j++){
              pface->addPhysicalEntity(physicalTags[j]);
            }
            pface->setParentEntity(face->getParentEntity());
            // Add to model
            model->add(pface);
            for(std::set<MElement*>::iterator itSet = connectedElements[i].begin();
                itSet != connectedElements[i].end(); ++itSet){
              // Add elements
              pface->addElement((*itSet)->getType(), (*itSet));
            }
          }

          model->remove(face);
          face->triangles.clear();
          face->quadrangles.clear();
          face->mesh_vertices.clear();
          delete face;
        }

        connectedElements.clear();
      }
    }
  }

  if (meshDim >= 2){ // Create partition edges
    Msg::Info(" - Creating partition edges");

    if (meshDim == 2){
      for(unsigned int i = 0; i < model->getNumPartitions(); i++){
        for(std::set<MElement*>::iterator it = boundaryElements[i].begin();
            it != boundaryElements[i].end(); ++it){
          for(int j = 0; j < (*it)->getNumEdges(); j++){
            edgeToElement[(*it)->getEdge(j)].push_back
            (std::pair<MElement*, std::vector<unsigned int> >
             (*it, std::vector<unsigned int>(1,i)));
          }
        }
      }
    }
    else{
      Graph subGraph(model);
      MakeGraph(model, subGraph, 2);
      createDualGraph(subGraph, false);
      unsigned int *part = new unsigned int[subGraph.ne()];
      int partIndex = 0;
      
      std::map<unsigned int, std::vector<unsigned int> > mapOfPartitions;
      unsigned int mapOfPartitionsTag = 0;
      for(GModel::const_fiter it = model->firstFace(); it != model->lastFace(); ++it){
        if((*it)->geomType() == GEntity::PartitionSurface){
          std::vector<unsigned int> partitions = static_cast<partitionFace*>(*it)->getPartitions();
          mapOfPartitions.insert(std::pair<unsigned int, std::vector<unsigned int> >(mapOfPartitionsTag, partitions));
          // Must absolutely be in the same order as in the MakeGraph function
          for(std::vector<MTriangle*>::iterator itElm = (*it)->triangles.begin(); itElm != (*it)->triangles.end(); ++itElm)
            part[partIndex++] = mapOfPartitionsTag;
          for(std::vector<MQuadrangle*>::iterator itElm = (*it)->quadrangles.begin(); itElm != (*it)->quadrangles.end(); ++itElm)
            part[partIndex++] = mapOfPartitionsTag;
          mapOfPartitionsTag++;
        }
      }
      subGraph.partition(part);
      
      std::vector< std::set<MElement*> > subBoundaryElements = subGraph.getBoundaryElements(mapOfPartitionsTag);
      
      for(unsigned int i = 0; i < mapOfPartitionsTag; i++){
        for(std::set<MElement*>::iterator it = subBoundaryElements[i].begin();
            it != subBoundaryElements[i].end(); ++it){
          for(int j = 0; j < (*it)->getNumEdges(); j++){
            edgeToElement[(*it)->getEdge(j)].push_back
            (std::pair<MElement*, std::vector<unsigned int> >
             (*it, mapOfPartitions[i]));
          }
        }
      }
    }
    for(hashmapedge::const_iterator it = edgeToElement.begin();
        it != edgeToElement.end(); ++it){
      MEdge e = it->first;

      std::vector<unsigned int> partitions;
      getPartitionInVector(partitions, it->second);
      if(partitions.size() < 2) continue;

      MElement* reference = getReferenceElement(it->second);
      if(!reference) continue;

      assignPartitionBoundary(model, e, reference, partitions, pedges, elementToEntity);
    }
    edgeToElement.clear();

    // Divide the non connected entities
    std::set<GEdge*, GEntityLessThan> edges = model->getEdges();
    for(GModel::const_eiter it = edges.begin(); it != edges.end(); ++it){
      if((*it)->geomType() == GEntity::PartitionCurve){
        partitionEdge* edge = static_cast<partitionEdge*>(*it);
        AssignMeshVerticesToEntity(edge, false);

        // We build a graph
        Graph graph(model);
        graph.ne(edge->getNumMeshElements());
        graph.nn(edge->getNumMeshVertices());
        graph.dim(1);
        graph.elementResize(graph.ne());
        graph.vertexResize(model->getMaxVertexNumber());
        graph.eptrResize(graph.ne()+1);
        graph.eptr(0,0);
        const int eindSize = getSizeOfEind(edge);
        graph.eindResize(eindSize);

        int eptrIndex = 0;
        int eindIndex = 0;
        int numVertex = 0;

        fillElementsToNodesMap(graph, edge, eptrIndex, eindIndex, numVertex,
                               edge->lines.begin(), edge->lines.end());
        createDualGraph(graph, false);

        std::vector< std::set<MElement*> > connectedElements;
        fillConnectedElements(connectedElements, graph);
        graph.clear();

        if(connectedElements.size() > 1){
          for(unsigned int i = 0; i < connectedElements.size(); i++){
            // Create the new partitionEdge
            partitionEdge *pedge = new partitionEdge
              (model, model->getMaxElementaryNumber(1)+1, 0, 0, edge->getPartitions());
            // Assign physicals and parent entity
            std::vector<int> physicalTags = edge->getPhysicalEntities();
            for(unsigned int j = 0; j < physicalTags.size(); j++){
              pedge->addPhysicalEntity(physicalTags[j]);
            }
            pedge->setParentEntity(edge->getParentEntity());
            // Add to model
            model->add(pedge);
            for(std::set<MElement*>::iterator itSet = connectedElements[i].begin();
                itSet != connectedElements[i].end(); ++itSet){
              // Add elements
              pedge->addElement((*itSet)->getType(), (*itSet));
            }
          }

          model->remove(edge);
          edge->lines.clear();
          edge->mesh_vertices.clear();
          delete edge;
        }

        connectedElements.clear();
      }
    }
  }

  if (meshDim >= 1){ // Create partition vertices
    Msg::Info(" - Creating partition vertices");
    if (meshDim == 1){
      for(unsigned int i = 0; i < model->getNumPartitions(); i++){
        for(std::set<MElement*>::iterator it = boundaryElements[i].begin();
            it != boundaryElements[i].end(); ++it){
          for(int j = 0; j < (*it)->getNumPrimaryVertices(); j++){
            vertexToElement[(*it)->getVertex(j)].push_back
            (std::pair<MElement*, std::vector<unsigned int> >
             (*it, std::vector<unsigned int>(1,i)));
          }
        }
      }
    }
    else{
      Graph subGraph(model);
      MakeGraph(model, subGraph, 1);
      createDualGraph(subGraph, false);
      unsigned int *part = new unsigned int[subGraph.ne()];
      int partIndex = 0;
      
      std::map<unsigned int, std::vector<unsigned int> > mapOfPartitions;
      unsigned int mapOfPartitionsTag = 0;
      for(GModel::const_eiter it = model->firstEdge(); it != model->lastEdge(); ++it){
        if((*it)->geomType() == GEntity::PartitionCurve){
          std::vector<unsigned int> partitions = static_cast<partitionEdge*>(*it)->getPartitions();
          mapOfPartitions.insert(std::pair<unsigned int, std::vector<unsigned int> >(mapOfPartitionsTag, partitions));
          // Must absolutely be in the same order as in the MakeGraph function
          for(std::vector<MLine*>::iterator itElm = (*it)->lines.begin(); itElm != (*it)->lines.end(); ++itElm)
            part[partIndex++] = mapOfPartitionsTag;
          mapOfPartitionsTag++;
        }
      }
      subGraph.partition(part);
      
      std::vector< std::set<MElement*> > subBoundaryElements = subGraph.getBoundaryElements(mapOfPartitionsTag);
      
      for(unsigned int i = 0; i < mapOfPartitionsTag; i++){
        for(std::set<MElement*>::iterator it = subBoundaryElements[i].begin();
            it != subBoundaryElements[i].end(); ++it){
          for(int j = 0; j < (*it)->getNumPrimaryVertices(); j++){
            vertexToElement[(*it)->getVertex(j)].push_back
            (std::pair<MElement*, std::vector<unsigned int> >
             (*it, mapOfPartitions[i]));
          }
        }
      }
    }
    for(hashmapvertex::const_iterator it = vertexToElement.begin();
        it != vertexToElement.end(); ++it){
      MVertex *v = it->first;

      std::vector<unsigned int> partitions;
      getPartitionInVector(partitions, it->second);
      if(partitions.size() < 2) continue;

      MElement* reference = getReferenceElement(it->second);
      if(!reference) continue;

      assignPartitionBoundary(model, v, reference, partitions, pvertices, elementToEntity);
    }
    vertexToElement.clear();

    // Divide the non connected entities
    std::set<GVertex*, GEntityLessThan> vertices = model->getVertices();
    for(GModel::const_viter it = vertices.begin(); it != vertices.end(); ++it){
      if((*it)->geomType() == GEntity::PartitionVertex){
        partitionVertex* vertex = static_cast<partitionVertex*>(*it);

        if(vertex->getNumMeshElements() > 1){
          for(unsigned int i = 0; i < vertex->getNumMeshElements(); i++){
            // Create the new partitionVertex
            partitionVertex *pvertex = new partitionVertex
              (model, model->getMaxElementaryNumber(0)+1, vertex->getPartitions());
            //Assign physicals and parent entity
            std::vector<int> physicalTags = vertex->getPhysicalEntities();
            for(unsigned int j = 0; j < physicalTags.size(); j++){
              pvertex->addPhysicalEntity(physicalTags[j]);
            }
            pvertex->setParentEntity(vertex->getParentEntity());
            // Add to model
            model->add(pvertex);
            // Add elements
            pvertex->addElement(vertex->getMeshElement(i)->getType(),
                                vertex->getMeshElement(i));
          }

          model->remove(vertex);
          vertex->points.clear();
          vertex->mesh_vertices.clear();
          delete vertex;
        }
      }
    }
  }
}

static std::string getPhysicalSubstr(GModel* model, GEntity* entity)
{
  std::string substr;
  std::vector<int> tags = entity->getPhysicalEntities();
  for(unsigned int i = 0; i < tags.size(); i++){
    std::string name = model->getPhysicalName(entity->dim(),tags[i]);

  std::string str;
  if(name[0] == '_'){
    for(unsigned int j = 0; j < name.length(); j++){
      if(name[j] == '{'){
        substr = str;
        break;
      }
      else if(name[j] == ' ') continue;
      else if(name[j] == '}') break;
      else{
        str += name[j];
      }
    }
  }

  }
  return substr;
}

static void addPhysical(GModel *const model, partitionRegion *parentEntity,
                        partitionFace *childEntity)
{
  std::string name;
  std::string parentSubStr = getPhysicalSubstr(model, parentEntity);
  if(parentSubStr.empty()){
    name = "_omega{";

    for(unsigned int i = 0; i < parentEntity->numPartitions(); i++){
      if(i > 0) name += ",";
#if __cplusplus >= 201103L
      name += std::to_string(parentEntity->getPartition(i));
#else
      char intToChar[20];
      sprintf(intToChar, "%d", parentEntity->getPartition(i));
      name += intToChar;
#endif
    }
    name += "}";

    int number = model->getPhysicalNumber(parentEntity->dim(), name);
    if(number == -1) number = model->numPhysicalNames()+1;
    model->setPhysicalName(name, parentEntity->dim(), number);
    parentEntity->addPhysicalEntity(number);

    parentSubStr = "_omega";
    name.clear();
  }

  std::string childSubStr = getPhysicalSubstr(model, childEntity);

  if(childEntity->numPartitions() == 1){
    name = "_omicron";
  }

  else{
    if(parentSubStr == "_omega"){
      name = "_sigma";
    }
    else if(parentSubStr == "_sigma"){
      name = "_tau";
    }
    else if(parentSubStr == "_tau"){
      name = "_upsilon";
    }
    else if(parentSubStr == "_upsilon"){
      return;
    }
    else if(parentSubStr == "_omicron"){
      name = "_omicronSigma";
    }
    else if(parentSubStr == "_omicronSigma"){
      name = "_omicronTau";
    }
    else if(parentSubStr == "_omicronTau"){
      return;
    }
  }

  if(childSubStr == name) return;
  name += "{";

  for(unsigned int i = 0; i < childEntity->numPartitions(); i++){
    if(i > 0) name += ",";
#if __cplusplus >= 201103L
    name += std::to_string(childEntity->getPartition(i));
#else
    char intToChar[20];
    sprintf(intToChar, "%d", childEntity->getPartition(i));
    name += intToChar;
#endif
  }
  name += "}";

  int number = model->getPhysicalNumber(childEntity->dim(), name);
  if(number == -1) number = model->numPhysicalNames()+1;
  model->setPhysicalName(name, childEntity->dim(), number);
  childEntity->addPhysicalEntity(number);
}

static void addPhysical(GModel *const model, partitionFace *parentEntity,
                        partitionEdge *childEntity)
{
  std::string name;
  std::string parentSubStr = getPhysicalSubstr(model, parentEntity);
  if(parentSubStr.empty()){
    name = "_omega{";

    for(unsigned int j = 0; j < parentEntity->numPartitions(); j++){
      if(j > 0) name += ",";
#if __cplusplus >= 201103L
      name += std::to_string(parentEntity->getPartition(j));
#else
      char intToChar[20];
      sprintf(intToChar, "%d", parentEntity->getPartition(j));
      name += intToChar;
#endif
    }
    name += "}";

    int number = model->getPhysicalNumber(parentEntity->dim(), name);
    if(number == -1) number = model->numPhysicalNames()+1;
    model->setPhysicalName(name, parentEntity->dim(), number);
    parentEntity->addPhysicalEntity(number);

    parentSubStr = "_omega";
    name.clear();
  }

  std::string childSubStr = getPhysicalSubstr(model, childEntity);

  if(childEntity->numPartitions() == 1){
    name = "_omicron";
  }
  else{
    if(parentSubStr == "_omega"){
      name = "_sigma";
    }
    else if(parentSubStr == "_sigma"){
      name = "_tau";
    }
    else if(parentSubStr == "_tau"){
      name = "_upsilon";
    }
    else if(parentSubStr == "_upsilon"){
      return;
    }
    else if(parentSubStr == "_omicron"){
      name = "_omicronSigma";
    }
    else if(parentSubStr == "_omicronSigma"){
      name = "_omicronTau";
    }
    else if(parentSubStr == "_omicronTau"){
      return;
    }
  }

  if(childSubStr == name) return;
  name += "{";

  for(unsigned int j = 0; j < childEntity->numPartitions(); j++){
    if(j > 0) name += ",";
#if __cplusplus >= 201103L
    name += std::to_string(childEntity->getPartition(j));
#else
    char intToChar[20];
    sprintf(intToChar, "%d", childEntity->getPartition(j));
    name += intToChar;
#endif
  }
  name += "}";

  int number = model->getPhysicalNumber(childEntity->dim(), name);
  if(number == -1) number = model->numPhysicalNames()+1;
  model->setPhysicalName(name, childEntity->dim(), number);
  childEntity->addPhysicalEntity(number);
}

static void addPhysical(GModel *const model, partitionEdge *parentEntity,
                        partitionVertex *childEntity)
{
  std::string name;
  std::string parentSubStr = getPhysicalSubstr(model, parentEntity);
  if(parentSubStr.empty()){
    name = "_omega{";

    for(unsigned int j = 0; j < parentEntity->numPartitions(); j++){
      if(j > 0) name += ",";
#if __cplusplus >= 201103L
      name += std::to_string(childEntity->getPartition(j));
#else
      char intToChar[20];
      sprintf(intToChar, "%d", childEntity->getPartition(j));
      name += intToChar;
#endif
    }
    name += "}";

    int number = model->getPhysicalNumber(parentEntity->dim(), name);
    if(number == -1) number = model->numPhysicalNames()+1;
    model->setPhysicalName(name, parentEntity->dim(), number);
    parentEntity->addPhysicalEntity(number);

    parentSubStr = "_omega";
    name.clear();
  }

  std::string childSubStr = getPhysicalSubstr(model, childEntity);

  if(childEntity->numPartitions() == 1){
    name = "_omicron";
  }
  else{
    if(parentSubStr == "_omega"){
      name = "_sigma";
    }
    else if(parentSubStr == "_sigma"){
      name = "_tau";
    }
    else if(parentSubStr == "_tau"){
      name = "_upsilon";
    }
    else if(parentSubStr == "_upsilon"){
      return;
    }
    else if(parentSubStr == "_omicron"){
      name = "_omicronSigma";
    }
    else if(parentSubStr == "_omicronSigma"){
      name = "_omicronTau";
    }
    else if(parentSubStr == "_omicronTau"){
      return;
    }
  }

  if(childSubStr == name) return;
  name += "{";

  for(unsigned int j = 0; j < childEntity->numPartitions(); j++){
    if(j > 0) name += ",";
#if __cplusplus >= 201103L
    name += std::to_string(childEntity->getPartition(j));
#else
    char intToChar[20];
    sprintf(intToChar, "%d", childEntity->getPartition(j));
    name += intToChar;
#endif
  }
  name += "}";

  int number = model->getPhysicalNumber(childEntity->dim(), name);
  if(number == -1) number = model->numPhysicalNames()+1;
  model->setPhysicalName(name, childEntity->dim(), number);
  childEntity->addPhysicalEntity(number);
}

static int computeOrientation(GEntity *parent, GEntity *child)
{
  std::vector<MVertex*> vertices;
  child->getMeshElement(0)->getVertices(vertices);

  for(unsigned int i = 0; i < parent->getNumMeshElements(); i++){
    std::vector<MVertex*> parentVertices;
    MElement* element = parent->getMeshElement(i);
    element->getVertices(parentVertices);
    bool haveBreak = false;
    for(unsigned int j = 0; j < vertices.size(); j++){
      std::vector<MVertex*>::iterator it = std::find
        (parentVertices.begin(), parentVertices.end(), vertices[j]);
      if(it == parentVertices.end()){
        haveBreak = true;
        break;
      }
    }

    if(!haveBreak){
      if(parent->dim() == 3){
        int numFace = 0;
        MFace me = child->getMeshElement(0)->getFace(0);

        for(int j = 0; j < element->getNumFaces(); j++){
          if(element->getFace(j) == me){
            numFace = j;
            break;
          }
        }

        std::vector<MVertex*> verts;
        element->getFaceVertices(numFace, verts);

        return (verts == vertices) ? 1 : -1;
      }
      else if(parent->dim() == 2){
        int numEdge = 0;
        MEdge me = child->getMeshElement(0)->getEdge(0);

        for(int j = 0; j < element->getNumEdges(); j++){
          if(element->getEdge(j) == me){
            numEdge = j;
            break;
          }
        }

        std::vector<MVertex*> verts;
        element->getEdgeVertices(numEdge, verts);

        return (verts == vertices) ? 1 : -1;
      }
      else if(parent->dim() == 1){
        return (vertices[0] == element->getVertex(0)) ? 1 : -1;
      }
    }
  }

  return 1;
}

// Build the topology
static void BuildTopology(GModel *model)
{
  // Loop over regions
  for(GModel::const_riter it = model->firstRegion(); it != model->lastRegion(); ++it){
    if((*it)->geomType() == GEntity::PartitionVolume){
      SBoundingBox3d boundingBox = (*it)->bounds();
      std::set<GFace*> boundaryFace;

      for(GModel::const_fiter itSub = model->firstFace(); itSub != model->lastFace(); ++itSub){
        if((*itSub)->geomType() == GEntity::PartitionSurface){
          SBoundingBox3d subBoundingBox = (*itSub)->bounds();
          if(boundingBox.contains(subBoundingBox)){
            boundaryFace.insert(*itSub);
          }
        }
      }

      std::list<GFace*> faces;
      std::list<int> facesOrientation;
      for(std::set<GFace*>::const_iterator itSet = boundaryFace.begin();
          itSet != boundaryFace.end(); ++itSet){
        if((*itSet)->geomType() == GEntity::PartitionSurface){
          (*itSet)->addRegion(*it);
          faces.push_back((*itSet));
          facesOrientation.push_back(computeOrientation(*it, *itSet));
          addPhysical(model, static_cast<partitionRegion*>(*it),
                      static_cast<partitionFace*>(*itSet));
        }
      }
      (*it)->set(faces);
      (*it)->setOrientations(facesOrientation);
    }
  }

  // Loop over faces
  for(GModel::const_fiter it = model->firstFace(); it != model->lastFace(); ++it){
    if((*it)->geomType() == GEntity::PartitionSurface){
      SBoundingBox3d boundingBox = (*it)->bounds();
      std::set<GEdge*> boundaryEdge;
      
      for(GModel::const_eiter itSub = model->firstEdge(); itSub != model->lastEdge(); ++itSub){
        if((*itSub)->geomType() == GEntity::PartitionCurve){
          SBoundingBox3d subBoundingBox = (*itSub)->bounds();
          if(boundingBox.contains(subBoundingBox)){
            boundaryEdge.insert(*itSub);
          }
        }
      }

      std::list<GEdge*> edges;
      std::list<int> edgesOrientation;
      for(std::set<GEdge*>::const_iterator itSet = boundaryEdge.begin();
          itSet != boundaryEdge.end(); ++itSet){
        if((*itSet)->geomType() == GEntity::PartitionCurve){
          (*itSet)->addFace(*it);
          edges.push_back((*itSet));
          edgesOrientation.push_back(computeOrientation(*it, *itSet));
          addPhysical(model, static_cast<partitionFace*>(*it),
                      static_cast<partitionEdge*>(*itSet));
        }
      }
      (*it)->set(edges);
      (*it)->setOrientations(edgesOrientation);
    }
  }

  // Loop over edges
  for(GModel::const_eiter it = model->firstEdge(); it != model->lastEdge(); ++it){
    if((*it)->geomType() == GEntity::PartitionCurve){
      SBoundingBox3d boundingBox = (*it)->bounds();
      std::set<GVertex*> boundaryVertex;
      
      for(GModel::const_viter itSub = model->firstVertex(); itSub != model->lastVertex(); ++itSub){
        if((*itSub)->geomType() == GEntity::PartitionVertex){
          SBoundingBox3d subBoundingBox = (*itSub)->bounds();
          if(boundingBox.contains(subBoundingBox)){
            boundaryVertex.insert(*itSub);
          }
        }
      }

      std::vector<GVertex*> vertices;
      std::list<int> verticesOrientation;
      for(std::set<GVertex*>::const_iterator itSet = boundaryVertex.begin();
          itSet != boundaryVertex.end(); ++itSet){
        if((*itSet)->geomType() == GEntity::PartitionVertex){
          (*itSet)->addEdge(*it);
          vertices.push_back((*itSet));
          verticesOrientation.push_back(computeOrientation(*it, *itSet));
          addPhysical(model, static_cast<partitionEdge*>(*it),
                      static_cast<partitionVertex*>(*itSet));
        }
      }

      if(vertices.size() == 2){
        if(verticesOrientation.front() == 1){
          (*it)->setBeginVertex(vertices[0]);
          (*it)->setEndVertex(vertices[1]);
        }
        else{
          (*it)->setBeginVertex(vertices[1]);
          (*it)->setEndVertex(vertices[0]);
        }
      }
      else if(vertices.size() == 1){
        if(verticesOrientation.front() == 1){
          (*it)->setBeginVertex(vertices[0]);
        }
        else{
          (*it)->setEndVertex(vertices[0]);
        }
      }
    }
  }
}

void movePeriodicNodesFromParentToPartitionEntities(GModel * const model)
{
  std::vector<GEntity*> entities;
  model->getEntities(entities);
  std::set<GEntity*> emptiedEntities;

  for(unsigned int i = 0; i < entities.size(); i++){
    if(entities[i]->correspondingVertices.size() != 0){
      emptiedEntities.insert(entities[i]);
      for(std::map<MVertex*,MVertex*>::iterator it =
            entities[i]->correspondingVertices.begin();
          it != entities[i]->correspondingVertices.end(); ++it){
        it->first->onWhat()->correspondingVertices.insert
          (std::pair<MVertex*,MVertex*>(it->first,it->second));
        it->first->onWhat()->setMeshMaster(it->second->onWhat());
      }
    }
  }

  for(std::set<GEntity*>::iterator it = emptiedEntities.begin();
      it != emptiedEntities.end(); ++it){
    (*it)->correspondingVertices.clear();
    (*it)->setMeshMaster(*it);
  }
}

void movePeriodicNodesFromPartitionToParentEntities(GModel * const model)
{
  std::vector<GEntity*> entities;
  model->getEntities(entities);
  std::set<GEntity*> emptiedEntities;

  for(unsigned int i = 0; i < entities.size(); i++){
    if(entities[i]->correspondingVertices.size() != 0){
      emptiedEntities.insert(entities[i]);
      for(std::map<MVertex*,MVertex*>::iterator it =
            entities[i]->correspondingVertices.begin();
          it != entities[i]->correspondingVertices.end(); ++it){
        if(entities[i]->geomType() == GEntity::PartitionVertex){
          partitionVertex* pv = static_cast<partitionVertex*>(entities[i]);
          pv->getParentEntity()->correspondingVertices.insert
            (std::pair<MVertex*,MVertex*>(it->first,it->second));
          static_cast<GEntity*>(pv->getParentEntity())->setMeshMaster
            (static_cast<partitionVertex*>(it->second->onWhat())->getParentEntity());
        }
        else if(entities[i]->geomType() == GEntity::PartitionCurve){
          partitionEdge* pe = static_cast<partitionEdge*>(entities[i]);
          pe->getParentEntity()->correspondingVertices.insert
            (std::pair<MVertex*,MVertex*>(it->first,it->second));
          static_cast<GEntity*>(pe->getParentEntity())->setMeshMaster
            (static_cast<partitionEdge*>(it->second->onWhat())->getParentEntity());
        }
        else if(entities[i]->geomType() == GEntity::PartitionSurface){
          partitionFace* pf = static_cast<partitionFace*>(entities[i]);
          pf->getParentEntity()->correspondingVertices.insert
            (std::pair<MVertex*,MVertex*>(it->first,it->second));
          static_cast<GEntity*>(pf->getParentEntity())->setMeshMaster
            (static_cast<partitionFace*>(it->second->onWhat())->getParentEntity());
        }
        else if(entities[i]->geomType() == GEntity::PartitionVolume){
          partitionRegion* pr = static_cast<partitionRegion*>(entities[i]);
          pr->getParentEntity()->correspondingVertices.insert
            (std::pair<MVertex*,MVertex*>(it->first,it->second));
          static_cast<GEntity*>(pr->getParentEntity())->setMeshMaster
            (static_cast<partitionRegion*>(it->second->onWhat())->getParentEntity());
        }
      }
    }
  }
}

// Partition a mesh into n parts. Returns: 0 = success, 1 = error
int PartitionMesh(GModel *const model)
{
  if(CTX::instance()->mesh.numPartitions <= 0) return 0;

  Msg::StatusBar(true, "Partitioning mesh...");
  double t1 = Cpu();

  Graph graph(model);
  if(MakeGraph(model, graph, -1)) return 1;
  graph.nparts(CTX::instance()->mesh.numPartitions);
  if(PartitionGraph(graph)) return 1;

  // Assign partitions to elements
  hashmap<MElement*, unsigned int> elmToPartition;
  for(unsigned int i = 0; i < graph.ne(); i++){
    if(graph.element(i)){
      if(graph.nparts() > 1){
        elmToPartition.insert(std::pair<MElement*, unsigned int>(graph.element(i),
                                                                 graph.partition(i)+1));
        // Should be removed
        graph.element(i)->setPartition(graph.partition(i)+1);
      }
      else{
        elmToPartition.insert(std::pair<MElement*, unsigned int>(graph.element(i), 1));
        // Should be removed
        graph.element(i)->setPartition(1);
      }
    }
  }
  model->setNumPartitions(graph.nparts());

  CreateNewEntities(model, elmToPartition);
  elmToPartition.clear();

  double t2 = Cpu();
  Msg::StatusBar(true, "Done partitioning mesh (%g s)", t2 - t1);

  if(CTX::instance()->mesh.partitionCreateTopology){
    Msg::StatusBar(true, "Creating partition topology...");
    std::vector< std::set<MElement*> > boundaryElements = graph.getBoundaryElements();
    double t3 = Cpu();
    Msg::Info(" + getBoundaryElements (%g s)", t3 - t2);
    CreatePartitionBoundaries(model, boundaryElements);
    double t4 = Cpu();
    Msg::Info(" + CreatePartitionBoundaries (%g s)", t4 - t3);
    boundaryElements.clear();
    BuildTopology(model);
    double t5 = Cpu();
    Msg::Info(" + BuildTopology (%g s)", t5 - t4);
    
    Msg::StatusBar(true, "Done creating partition topology (%g s)", t3 - t2);
  }

  AssignMeshVertices(model);

  movePeriodicNodesFromParentToPartitionEntities(model);

  if(CTX::instance()->mesh.partitionCreateGhostCells){
    graph.clearDualGraph();
    createDualGraph(graph, true);
    graph.assignGhostCells();
  }

  return 0;
}

template <class ITERATOR>
static void assignToParent(std::set<MVertex*> &verts, partitionRegion *region,
                           ITERATOR it_beg, ITERATOR it_end)
{
  for(ITERATOR it = it_beg; it != it_end; ++it){
    region->getParentEntity()->addElement((*it)->getType(), *it);
    (*it)->setPartition(0);

    for(int i = 0; i < (*it)->getNumVertices(); i++){
      if(verts.find((*it)->getVertex(i)) == verts.end()){
        (*it)->getVertex(i)->setEntity(region->getParentEntity());
        region->getParentEntity()->addMeshVertex((*it)->getVertex(i));
        verts.insert((*it)->getVertex(i));
      }
    }
  }
}

template <class ITERATOR>
static void assignToParent(std::set<MVertex*> &verts, partitionFace *face,
                           ITERATOR it_beg, ITERATOR it_end)
{
  for(ITERATOR it = it_beg; it != it_end; ++it){
    face->getParentEntity()->addElement((*it)->getType(), *it);
    (*it)->setPartition(0);

    for(int i = 0; i < (*it)->getNumVertices(); i++){
      if(verts.find((*it)->getVertex(i)) == verts.end()){
        (*it)->getVertex(i)->setEntity(face->getParentEntity());
        face->getParentEntity()->addMeshVertex((*it)->getVertex(i));
        verts.insert((*it)->getVertex(i));
      }
    }
  }
}

template <class ITERATOR>
static void assignToParent(std::set<MVertex*> &verts, partitionEdge *edge,
                           ITERATOR it_beg, ITERATOR it_end)
{
  for(ITERATOR it = it_beg; it != it_end; ++it){
    edge->getParentEntity()->addLine(reinterpret_cast<MLine*>(*it));
    (*it)->setPartition(0);

    for(int i = 0; i < (*it)->getNumVertices(); i++){
      if(verts.find((*it)->getVertex(i)) == verts.end()){
        (*it)->getVertex(i)->setEntity(edge->getParentEntity());
        edge->getParentEntity()->addMeshVertex((*it)->getVertex(i));
        verts.insert((*it)->getVertex(i));
      }
    }
  }
}

template <class ITERATOR>
static void assignToParent(std::set<MVertex*> &verts, partitionVertex *vertex,
                           ITERATOR it_beg, ITERATOR it_end)
{
  for(ITERATOR it = it_beg; it != it_end; ++it)
  {
    vertex->getParentEntity()->addPoint(reinterpret_cast<MPoint*>(*it));
    (*it)->setPartition(0);

    for(int i = 0; i < (*it)->getNumVertices(); i++){
      if(verts.find((*it)->getVertex(i)) == verts.end()){
        (*it)->getVertex(i)->setEntity(vertex->getParentEntity());
        vertex->getParentEntity()->addMeshVertex((*it)->getVertex(i));
        verts.insert((*it)->getVertex(i));
      }
    }
  }
}

// Un-partition a mesh and return to the initial mesh geomerty. Returns: 0 =
// success, 1 = error.
int UnpartitionMesh(GModel *const model)
{
  // make a copy so we can iterate safely (we will remove some entities)
  std::set<GRegion*, GEntityLessThan> regions = model->getRegions();
  std::set<GFace*, GEntityLessThan> faces = model->getFaces();
  std::set<GEdge*, GEntityLessThan> edges = model->getEdges();
  std::set<GVertex*, GEntityLessThan> vertices = model->getVertices();

  std::set<MVertex*> verts;

  movePeriodicNodesFromPartitionToParentEntities(model);

  // Loop over vertices
  for(GModel::viter it = vertices.begin(); it != vertices.end(); ++it){
    GVertex *vertex = *it;

    if(vertex->geomType() == GEntity::PartitionVertex){
      partitionVertex* pvertex = static_cast<partitionVertex*>(vertex);
      if(pvertex->getParentEntity()){
        assignToParent(verts, pvertex, pvertex->points.begin(), pvertex->points.end());
      }
      else{
        for(unsigned int j = 0; j < pvertex->points.size(); j++)
          delete pvertex->points[j];
      }
      pvertex->points.clear();
      pvertex->mesh_vertices.clear();

      model->remove(pvertex);
      delete pvertex;
    }
  }

  // Loop over edges
  for(GModel::eiter it = edges.begin(); it != edges.end(); ++it){
    GEdge *edge = *it;

    if(edge->geomType() == GEntity::PartitionCurve){
      partitionEdge* pedge = static_cast<partitionEdge*>(edge);
      if(pedge->getParentEntity()){
        assignToParent(verts, pedge, pedge->lines.begin(), pedge->lines.end());
      }
      else{
        for(unsigned int j = 0; j < pedge->lines.size(); j++)
          delete pedge->lines[j];
      }
      pedge->lines.clear();
      pedge->mesh_vertices.clear();
      pedge->setBeginVertex(0);
      pedge->setEndVertex(0);

      model->remove(pedge);
      delete pedge;
    }
    else if(edge->geomType() == GEntity::GhostCurve){
      model->remove(edge);
      delete edge;
    }
  }

  // Loop over faces
  for(GModel::fiter it = faces.begin(); it != faces.end(); ++it)
  {
    GFace *face = *it;

    if(face->geomType() == GEntity::PartitionSurface){
      partitionFace* pface = static_cast<partitionFace*>(face);
      if(pface->getParentEntity()){
        assignToParent(verts, pface, pface->triangles.begin(), pface->triangles.end());
        assignToParent(verts, pface, pface->quadrangles.begin(), pface->quadrangles.end());
      }
      else{
        for(unsigned int j = 0; j < pface->triangles.size(); j++)
          delete pface->triangles[j];
        for(unsigned int j = 0; j < pface->quadrangles.size(); j++)
          delete pface->quadrangles[j];
      }
      pface->triangles.clear();
      pface->quadrangles.clear();
      pface->mesh_vertices.clear();
      pface->set(std::list<GEdge*>());
      pface->setOrientations(std::list<int>());

      model->remove(pface);
      delete pface;
    }
    else if(face->geomType() == GEntity::GhostSurface){
      model->remove(face);
      delete face;
    }
  }

  // Loop over regions
  for(GModel::riter it = regions.begin(); it != regions.end(); ++it){
    GRegion *region = *it;

    if(region->geomType() == GEntity::PartitionVolume){
      partitionRegion* pregion = static_cast<partitionRegion*>(region);
      if(pregion->getParentEntity()){
        assignToParent(verts, pregion, pregion->tetrahedra.begin(), pregion->tetrahedra.end());
        assignToParent(verts, pregion, pregion->hexahedra.begin(), pregion->hexahedra.end());
        assignToParent(verts, pregion, pregion->prisms.begin(), pregion->prisms.end());
        assignToParent(verts, pregion, pregion->pyramids.begin(), pregion->pyramids.end());
        assignToParent(verts, pregion, pregion->trihedra.begin(), pregion->trihedra.end());
      }
      else{
        for(unsigned int j = 0; j < pregion->tetrahedra.size(); j++)
          delete pregion->tetrahedra[j];

        for(unsigned int j = 0; j < pregion->hexahedra.size(); j++)
          delete pregion->hexahedra[j];

        for(unsigned int j = 0; j < pregion->prisms.size(); j++)
          delete pregion->prisms[j];

        for(unsigned int j = 0; j < pregion->pyramids.size(); j++)
          delete pregion->pyramids[j];

        for(unsigned int j = 0; j < pregion->trihedra.size(); j++)
          delete pregion->trihedra[j];
      }
      pregion->tetrahedra.clear();
      pregion->hexahedra.clear();
      pregion->prisms.clear();
      pregion->pyramids.clear();
      pregion->trihedra.clear();
      pregion->mesh_vertices.clear();
      pregion->set(std::list<GFace*>());
      pregion->setOrientations(std::list<int>());

      model->remove(pregion);
      delete pregion;
    }
    else if(region->geomType() == GEntity::GhostVolume){
      model->remove(region);
      delete region;
    }
  }

  model->setNumPartitions(0);

  std::map<std::pair<int, int>, std::string> physicalNames = model->getPhysicalNames();
  for(GModel::piter it = physicalNames.begin(); it != physicalNames.end(); ++it){
    std::string name = it->second;

    if(name[0] == '_'){
      model->removePhysicalGroup(it->first.first, it->first.second);
    }
  }

  return 0;
}

// Create the partition according to the element split given by elmToPartition
// Returns: 0 = success, 1 = no elements found.
int PartitionUsingThisSplit(GModel *const model, unsigned int npart,
                            hashmap<MElement*, unsigned int> &elmToPartition)
{
  Graph graph(model);
  if(MakeGraph(model, graph, -1)) return 1;
  createDualGraph(graph, false);
  graph.nparts(npart);

  if(elmToPartition.size() != graph.ne()){
    Msg::Error("All elements are not partitioned.");
    return 1;
  }

  unsigned int *part = new unsigned int[graph.ne()];
  for(unsigned int i = 0; i < graph.ne(); i++){
    if(graph.element(i)){
      part[i] = elmToPartition[graph.element(i)]-1;
    }
  }

  // Check and correct the topology
  for(unsigned int i = 0; i < graph.ne(); i++){
    if(graph.element(i)->getDim() == (int)graph.dim()) continue;

    for(unsigned int j = graph.xadj(i); j < graph.xadj(i+1); j++){
      if(graph.element(graph.adjncy(j))->getDim() == graph.element(i)->getDim()+1){
        if(part[i] != part[graph.adjncy(j)]){
          part[i] = part[graph.adjncy(j)];
          elmToPartition[graph.element(i)] = part[i]+1;
          break;
        }
      }

      if(graph.element(graph.adjncy(j))->getDim() == graph.element(i)->getDim()+2){
        if(part[i] != part[graph.adjncy(j)]){
          part[i] = part[graph.adjncy(j)];
          elmToPartition[graph.element(i)] = part[i]+1;
          break;
        }
      }

      if(graph.element(graph.adjncy(j))->getDim() == graph.element(i)->getDim()+3){
        if(part[i] != part[graph.adjncy(j)]){
          part[i] = part[graph.adjncy(j)];
          elmToPartition[graph.element(i)] = part[i]+1;
          break;
        }
      }
    }
  }
  graph.partition(part);

  model->setNumPartitions(graph.nparts());

  CreateNewEntities(model, elmToPartition);
  elmToPartition.clear();

  if(CTX::instance()->mesh.partitionCreateTopology){
    Msg::StatusBar(true, "Creating partition topology...");
    std::vector< std::set<MElement*> > boundaryElements = graph.getBoundaryElements();
    CreatePartitionBoundaries(model, boundaryElements);
    boundaryElements.clear();
    BuildTopology(model);
    Msg::StatusBar(true, "Done creating partition topology");
  }

  AssignMeshVertices(model);

  movePeriodicNodesFromParentToPartitionEntities(model);

  if(CTX::instance()->mesh.partitionCreateGhostCells){
    graph.clearDualGraph();
    createDualGraph(graph, false);
    graph.assignGhostCells();
  }

  return 0;
}

// Import a mesh partitionned by a tag given to the element and create the
// topology (omega, sigma, bndSigma, ...). Returns: 0 = success, 1 = no elements
// found.
int ConvertOldPartitioningToNewOne(GModel *const model)
{
  Msg::StatusBar(true, "Converting old partitioning...");

  hashmap<MElement*, unsigned int> elmToPartition;
  std::set<unsigned int> partitions;
  std::vector<GEntity*> entities;
  model->getEntities(entities);
  for(unsigned int i = 0; i < entities.size(); i++){
    for(unsigned int j = 0; j < entities[i]->getNumMeshElements(); j++){
      MElement *e = entities[i]->getMeshElement(i);
      elmToPartition.insert(std::pair<MElement*, unsigned int>(e, e->getPartition()));
      partitions.insert(e->getPartition());
    }
  }

  return PartitionUsingThisSplit(model, partitions.size(), elmToPartition);
}

#else

int PartitionMesh(GModel *const model)
{
  Msg::Error("Gmsh must be compiled with METIS support to partition meshes");
  return 0;
}

int UnpartitionMesh(GModel *const model)
{
}

int ConvertOldPartitioningToNewOne(GModel *const model)
{
}

int PartitionUsingThisSplit(GModel *const model, unsigned int npart,
                            hashmap<MElement*, unsigned int> &elmToPartition)
{
}

#endif
