/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2014 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed-code.org for more information.

   This file is part of plumed, version 2.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "LinkCells.h"
#include "Communicator.h"
#include "Tools.h"

namespace PLMD{

LinkCells::LinkCells( Communicator& cc ) :
comm(cc),
cutoffwasset(false),
ncells(3),
nstride(3)
{
}

void LinkCells::setCutoff( const double& lcut ){
  cutoffwasset=true; link_cutoff=lcut;
}

void LinkCells::buildCellLists( const std::vector<Vector>& pos, const std::vector<unsigned>& indices, const Pbc& pbc ){
  plumed_assert( cutoffwasset && pos.size()==indices.size() );

  // Must be able to check that pbcs are not nonsensical in some way?? -- GAT

  // Setup the pbc object by copying it from action
  mypbc.setBox( pbc.getBox() );

  // Setup the lists
  if( pos.size()!=allcells.size() ){ 
    allcells.resize( pos.size() ); lcell_lists.resize( pos.size() ); 
  }

  ncells[0] = std::floor( mypbc.getBox().getRow(0).modulo() / link_cutoff );
  if( ncells[0]==0 ) ncells[0]=1;
  ncells[1] = std::floor( mypbc.getBox().getRow(1).modulo() / link_cutoff );
  if( ncells[1]==0 ) ncells[1]=1;
  ncells[2] = std::floor( mypbc.getBox().getRow(2).modulo() / link_cutoff );
  if( ncells[2]==0 ) ncells[2]=1;
  // Setup the strides
  nstride[0]=1; nstride[1]=ncells[0]; nstride[2]=ncells[0]*ncells[1];

  // Setup the storage for link cells
  unsigned ncellstot=ncells[0]*ncells[1]*ncells[2];
  if( lcell_tots.size()!=ncellstot ){
      lcell_tots.resize( ncellstot ); lcell_starts.resize( ncellstot );
  }
  // Clear nlcells
  for(unsigned i=0;i<ncellstot;++i) lcell_tots[i]=0;
  // Clear allcells
  allcells.assign( allcells.size(), 0 );

  // Find out what cell everyone is in
  unsigned rank=comm.Get_rank(), size=comm.Get_size();
  for(unsigned i=rank;i<pos.size();i+=size){
      allcells[i]=findCell( pos[i] );
      lcell_tots[allcells[i]]++;
  }
  // And gather all this information on every node
  comm.Sum( allcells ); comm.Sum( lcell_tots );

  // Now prepare the link cell lists
  unsigned tot=0;
  for(unsigned i=0;i<lcell_tots.size();++i){ lcell_starts[i]=tot; tot+=lcell_tots[i]; lcell_tots[i]=0; }
  plumed_assert( tot==pos.size() );

  // And setup the link cells properly
  for(unsigned j=0;j<pos.size();++j){
      unsigned myind = lcell_starts[ allcells[j] ] + lcell_tots[ allcells[j] ];
      lcell_lists[ myind ] = indices[j];
      lcell_tots[allcells[j]]++;
  }
}

#define LINKC_MIN(n) ((n<2)? 0 : -1)
#define LINKC_MAX(n) ((n<3)? 1 : 2)
#define LINKC_PBC(n,num) ((n<0)? num-1 : n%num )

void LinkCells::retrieveNeighboringAtoms( const Vector& pos, unsigned& natomsper, std::vector<unsigned>& atoms ){
  plumed_assert( natomsper==1 );  // This is really a bug. If you are trying to reuse this ask GAT for help
  std::vector<unsigned> celn( findMyCell( pos ) );

  for(int nx=LINKC_MIN(ncells[0]);nx<LINKC_MAX(ncells[0]);++nx){
     int xval = celn[0] + nx;  
     xval=LINKC_PBC(xval,ncells[0])*nstride[0]; 
     for(int ny=LINKC_MIN(ncells[1]);ny<LINKC_MAX(ncells[1]);++ny){
         int yval = celn[1] + ny;  
         yval=LINKC_PBC(yval,ncells[1])*nstride[1]; 
         for(int nz=LINKC_MIN(ncells[2]);nz<LINKC_MAX(ncells[2]);++nz){
             int zval = celn[2] + nz;
             zval=LINKC_PBC(zval,ncells[2])*nstride[2]; 

             unsigned mybox=xval+yval+zval;
             for(unsigned k=0;k<lcell_tots[mybox];++k){
                 unsigned myatom = lcell_lists[lcell_starts[mybox]+k];
                 if( myatom!=atoms[0] ){  // Ideally would provide an option to not do this
                     atoms[natomsper]=myatom;
                     natomsper++;
                 } 
             }
         }
     }
  }
}

std::vector<unsigned> LinkCells::findMyCell( const Vector& pos ) const {
  Vector fpos=mypbc.realToScaled( pos ); std::vector<unsigned> celn(3);
  for(unsigned j=0;j<3;++j){
     celn[j] = std::floor( ( Tools::pbc(fpos[j]) + 0.5 ) * ncells[j] );
     plumed_assert( celn[j]>=0 && celn[j]<ncells[j] ); // Check that atom is in box  
  }
  return celn;
}

unsigned LinkCells::findCell( const Vector& pos ) const {
  std::vector<unsigned> celn( findMyCell(pos ) );
  return celn[0]*nstride[0] + celn[1]*nstride[1] + celn[2]*nstride[2];
}


}
