// $Id: STL.cpp,v 1.6 2002-05-18 08:01:39 geuzaine Exp $
//
// Copyright (C) 1997 - 2002 C. Geuzaine, J.-F. Remacle
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.

#include "Gmsh.h"
#include "Mesh.h"
#include "STL.h"

extern Mesh *THEM;

STL_Data::STL_Data (){
  Vertices = Tree_Create (sizeof (Vertex *), comparePosition);
  Simplexes = Tree_Create (sizeof (Simplex *), compareSimplex);
  LSimplexes = NULL;
  LVertices = NULL;
}

STL_Data::~STL_Data (){
}

void STL_Data::Add_Facet (double x1, double y1, double z1,
                          double x2, double y2, double z2,
                          double x3, double y3, double z3){
  Vertex **ppv;
  Vertex *v1 = Create_Vertex (Tree_Nbr (Vertices) + 1, x1, y1, z1, 1, 0);

  if ((ppv = (Vertex **) Tree_PQuery (Vertices, &v1))){
    delete v1;
    v1 = *ppv;
  }
  else{
    Tree_Add (Vertices, &v1);
    Tree_Add (THEM->Points, &v1);
  }

  Vertex *v2 = Create_Vertex (Tree_Nbr (Vertices) + 1, x2, y2, z2, 1, 0);
  if ((ppv = (Vertex **) Tree_PQuery (Vertices, &v2))){
    delete v2;
    v2 = *ppv;
  }
  else{
    Tree_Add (Vertices, &v2);
    Tree_Add (THEM->Points, &v2);
  }

  Vertex *v3 = Create_Vertex (Tree_Nbr (Vertices) + 1, x3, y3, z3, 1, 0);
  if ((ppv = (Vertex **) Tree_PQuery (Vertices, &v3))){
    delete v3;
    v3 = *ppv;
  }
  else{
    Tree_Add (Vertices, &v3);
    Tree_Add (THEM->Points, &v3);
  }
  Simplex *s = Create_Simplex (v1, v2, v3, NULL);
  Tree_Add (Simplexes, &s);
}

void STL_Data::GetFacet (int ifac, int &v1, int &v2, int &v3){
  Simplex *s;
  if (!LSimplexes)
    LSimplexes = Tree2List (Simplexes);
  List_Read (LSimplexes, ifac - 1, &s);
  v1 = s->V[0]->Num;
  v2 = s->V[1]->Num;
  v3 = s->V[2]->Num;
}

void STL_Data::GetVertex (int i, double &x, double &y, double &z){
  Vertex *v;
  if (!LVertices)
    LVertices = Tree2List (Vertices);
  List_Read (LVertices, i - 1, &v);
  x = v->Pos.X;
  y = v->Pos.Y;
  z = v->Pos.Z;
}
