#include<iostream>
#include<math.h>
#include<iomanip>
#include <time.h>
#include "simulator.h"
#include "MutSolver.h"

using namespace std;



EdgePtr GraphBuilder::getRandomEdgeOnTree(double & dSplitPoint,
double dRandomSpot){
    if (this->bEndGeneConversion && dSplitPoint!=-1.){
        dSplitPoint = gcNewEdge->getBottomNodeRef()->getHeight()+0.;
        return this->gcNewEdge;
    }
    bool found = false;
    double dRunningLength = 0.0;
    EdgePtrVector::iterator it = pEdgeVectorInTree->begin();
    EdgePtr curEdge;
    // the linked list of tree edges are lined up so that a uniform point
    // on the tree can be selected for where the crossover occurs
    unsigned int counter=0;
    while(!found && counter<iTotalTreeEdges){
        curEdge = *it;
        if (!curEdge->bDeleted){
            if (dRandomSpot<dRunningLength+curEdge->getLength()){
                // the random spot is within the current segment
                // the reference var splitpoint returns the position
                // relative to the bottom of this edge that the xover
                // occurs
                dSplitPoint = curEdge->getBottomNodeRef()->getHeight()+
                (dRandomSpot - dRunningLength);
                found = true;
            }else{
                dRunningLength+=curEdge->getLength();

            }
        }
        ++counter;
        ++it;
    }
    if (!found) throw "RandomSpot was out of range for xover";
    return curEdge;
}

EdgePtr GraphBuilder::getRandomEdgeToCoalesce(EdgePtr & coalescingEdge,
double dHeight){
    if (this->bEndGeneConversion){
        return this->gcOldEdge;
    }
    int iPopulation = coalescingEdge->getBottomNodeRef()->getPopulation();
    EdgePtrVector & pEdgeVector = this->pEdgeVectorByPop->at(iPopulation);
    EdgeIndexQueue & pVectorIndicesToRecycle = this->pVectorIndicesToRecycle->at(iPopulation);
    unsigned int edgeVectorSize = pEdgeVector.size();
    bool found = false;
    int iUnifPick =-1;
    while(!found){
        // guessing by randomly selecting elements from the vector of graph
        // edges appears to work pretty well here
        iUnifPick = static_cast<int>(pRandNumGenerator->unifRV()*
        edgeVectorSize);
        EdgePtr edge = pEdgeVector[iUnifPick];
        if (edge->bDeleted){
            // if this is marked for deletion, take the opportunity
            // to store the vector index in a queue so this element can
            // recycled when a new edge is to be added
            if (!edge->bInQueue){
                pVectorIndicesToRecycle.push(iUnifPick);
                edge->bInQueue = true;
            }
        // if the proposed coalescing time is within the endpoints of this
        // randomly picked edge, select this edge
        }else if (edge->getBottomNodeRef()->getHeight()<dHeight
         && edge->getTopNodeRef()->getHeight()>dHeight) found = true;
    }
    EdgePtr & edge = pEdgeVector[iUnifPick];
    if (pConfig->bDebug){
        cerr<<"Edge to coalesce: "<<edge->getBottomNodeRef()->getHeight()<<" to "<<
        edge->getTopNodeRef()->getHeight()<<endl;
    }

    return edge;
}

void GraphBuilder::mutateBelowEdge(EdgePtr & edge){
    NodePtr & bottomNode = edge->getBottomNodeRef();
    if (bottomNode->getType()==Node::SAMPLE  ){
        SampleNode* sample = static_cast<SampleNode*>(bottomNode.get());
        // mark this sampled node as affected.
        sample->bAffected = true;
    }else{
        for (unsigned int i=0;i<bottomNode->getBottomEdgeSize();++i){
            #ifdef DIAG
            if (bottomNode->getBottomEdgeByIndex(i)->bDeleted){
                throw "Mutate below edge: this edge should have been removed";
            }
            #endif
            EdgePtr edge = bottomNode->getBottomEdgeByIndex(i);
            if (edge->iGraphIteration==iGraphIteration){
                mutateBelowEdge(edge);
            }
        }
    }
}

bool GraphBuilder::markEdgesAbove(bool bFirstSample,bool  bCalledFromParent,
EdgePtr & selectedEdge, unsigned int & iSampleIndex){
    if (bFirstSample || bCalledFromParent || !selectedEdge->bInCurrentTree){
        if (!selectedEdge->bInCurrentTree) addEdgeToCurrentTree(selectedEdge);
        NodePtr topNode = selectedEdge->getTopNodeRef();
        double dTopHeight = topNode->getHeight();
        double dproposedOriginHt = localMRCA->getHeight();
        if (dTopHeight>=dproposedOriginHt &&
        topNode->getType()==Node::COAL){
            // If we reach at least as high as proposed MRCA
            // save this edge as the vector
            // so we don't traverse from the bottom
            pTreeEdgesToCoalesceArray[iSampleIndex]=selectedEdge;
            if (bFirstSample){
                if (dTopHeight>dproposedOriginHt)
                    // establish a new proposed MRCA
                    // for susequent samples to try reaching
                    localMRCA = topNode;
                // proceed to the next sampled node
                return true;
            }else{
                if (dTopHeight>dproposedOriginHt){
                    // if we surpassed a MRCA height previously proposed
                    // upgrade the proposed MRCA height
                    localMRCA = topNode;
                    // start over in the loop to sample 0
                    return false;
                }else{
                    if (localMRCA!=topNode){
                        cerr<<"proposed grandMRCA != top Node\n";
                    }
                    // here the current height is EQUAL to the proposed MRCA,
                    // this is good and we can proceed to the next sampled node
                    return true;
                }
            }
        }
        else{
            // if we come across a crossover, there are two edges above,
            // we choose the one with the more recent (larger) tree label
            if (topNode->getType()==Node::XOVER){
                EdgePtr edge0 =topNode->getTopEdgeByIndex(0);
                EdgePtr edge1 =topNode->getTopEdgeByIndex(1);
                int iter0=-1,iter1=-1;
                if (edge0!=NULL) iter0=edge0->iGraphIteration;
                if (edge1!=NULL) iter1=edge1->iGraphIteration;
                if (iter0==-1 && iter1==-1) throw "In mark edges above, both edges above xover were missing";
                EdgePtr & nextEdge = (iter0>iter1)? edge0:edge1;
                return markEdgesAbove(bFirstSample,false,nextEdge,
                iSampleIndex);
            // for other nodes, just choose the single edge above it
            // recursively call this method again
            }else{
                EdgePtr edge = topNode->getTopEdgeByIndex(0);
                return markEdgesAbove(bFirstSample,false,edge,
                iSampleIndex);
            }
        }
    }else{
        // the selected edge was in the current tree already and this is for
        // a sampled node after the first one and was called by this method.
        // the latter condition implies that we previously explored up this
        // node.  in this case, save time and proceed to the next node
        return true;
    }
}

void GraphBuilder::markEdgesBelow(EdgePtr & selectedEdge){
    selectedEdge->iGraphIteration = iGraphIteration;
    NodePtr  & bottomNode = selectedEdge->getBottomNodeRef();
    if (bottomNode->getType()==Node::COAL||
    bottomNode->getType()==Node::SAMPLE) return;
    else{
        EdgePtr bottomEdge = bottomNode->getBottomEdgeByIndex(0);
        markEdgesBelow(bottomEdge);
    }

}

void GraphBuilder::pruneEdgesBelow(EdgePtr & selectedEdge){
    NodePtr & bottomNode = selectedEdge->getBottomNodeRef();
    if (bottomNode->getType()==Node::COAL){
        // this is the terminating condition and should be the new
        // lower MRCA
        grandMRCA = bottomNode;
        grandMRCA->clearTopEdges();
    }else{
        // we didn't find a coalescent node (MRCA) so keep searching down
        bottomNode->removeEvent();
        EdgePtr bottomEdge = bottomNode->getBottomEdgeByIndex(0);
        pruneEdgesBelow(bottomEdge);
    }
    deleteEdge(selectedEdge);
}

void GraphBuilder::pruneEdgesAbove(EdgePtr & selectedEdge){
    NodePtr & topNode = selectedEdge->getTopNodeRef();
    short int topNodeType = topNode->getType();
    if (topNodeType==Node::COAL){
        EdgePtr edge0 = topNode->getBottomEdgeByIndex(0);
        EdgePtr edge1 = topNode->getBottomEdgeByIndex(1);
        if (topNode->getTopEdgeSize()==0){
            #ifdef DIAG
            if (topNode!=grandMRCA) throw "Top node is NOT grandMRCA!\n";
            #endif
            EdgePtr & otherEdge = edge0==selectedEdge ?
            edge1 : edge0;
            pruneEdgesBelow(otherEdge);
        }else{
            // just some coal node below the grandMRCA. fuse the other two edges.
            EdgePtr edgeAboveNode = topNode->getTopEdgeByIndex(0);
            EdgePtr & edgeBelowNode = edge0==
            selectedEdge?edge1:edge0;
            mergeEdges(edgeAboveNode,edgeBelowNode);
        }

    }else{
        #ifdef DIAG
        if (topNodeType!=Node::MIGRATION
        && topNodeType!=Node::QUERY){
            cerr<<"At edge with "<<selectedEdge->getBottomNodeRef()->getHeight()<<
            " and "<<selectedEdge->getTopNodeRef()->getHeight()<<endl;
            throw "Prune up did not find a migration node";
        }
        #endif
        EdgePtr topEdge = topNode->getTopEdgeByIndex(0);
        pruneEdgesAbove(topEdge);
    }
    topNode->removeEvent();
    deleteEdge(selectedEdge);
}
//llf added
/*
boole GraphBuilder::isOnLocalTree(NodePtr pNode)
{

      
}; */     

void GraphBuilder::getMutEdgeVector(EdgePtrVector& mutEdgeVector)
{
//    double dTotalMutEdgeLen(0.0);
    EdgePtrVector::iterator it = pEdgeVectorInTree->begin();
    EdgePtrVector::iterator itEnd = pEdgeVectorInTree->end();
    EdgePtr curEdge;
    double dHeight = 0.0;
    double TopHeight;
    NodePtr TopNode;
    //cerr<<"iCurrentTreeId:"<<this->iCurrentTreeId<<endl;
    while(it != itEnd){
        curEdge = *it;
        if (!curEdge->bDeleted){
            if(curEdge->iGraphIteration == this->iCurrentTreeId){
                   NodePtr tmpNode = TracebacktoCurrentTree(curEdge);
                   EdgePtr edge0 = tmpNode->getTopEdgeByIndex(0);
                   EdgePtr edge1 = tmpNode->getTopEdgeByIndex(1);
                   EdgePtr tmpEdge;  
                   int iter0=-1,iter1=-1;
                    if (edge0!=NULL) iter0=edge0->iGraphIteration;
                    if (edge1!=NULL) iter1=edge1->iGraphIteration;
                     if(iter0==this->iCurrentTreeId){
                          tmpEdge = edge0;
                     }else if(iter1==this->iCurrentTreeId){
                          tmpEdge = edge1; 
                     }                    
                   NodePtr testNode = tmpEdge->getTopNodeRef();
                  if(tmpEdge->getMutNum() > 0){
                      mutEdgeVector.push_back(curEdge);
                      if(testNode->getHeight() > dHeight){
                        dHeight = testNode->getHeight();  
                        TopNode =  tmpNode;           
                      }                  
                  }                            
            }
        }
        it++;
    } 
                 
    it = mutEdgeVector.begin();
    while(it != mutEdgeVector.end()){
        curEdge = *it;
       NodePtr tmpNode = TracebacktoCurrentTree(curEdge);
          if(tmpNode == TopNode){
              it = mutEdgeVector.erase(it);
          }else{
              it++;
          }                  
    }         
    
                                           
}

EdgePtr GraphBuilder::selectRandomEdgeWithMutation(double& dSplitPoint)
{
    //cerr<<"entry of selectRandomEdgeWithMutation"<<endl;
    //find all mutation edge.    
    EdgePtrVector mutEdgeVector;
    getMutEdgeVector(mutEdgeVector);
    if(mutEdgeVector.empty())
        throw"mutEdgeVector is empty";
    //cerr<<"end if"<<endl;
    //randomly select one edge from mutation edge vector.
    double dTotalMutEdgeLen=0.0;
    EdgePtrVector::iterator it = mutEdgeVector.begin();
    EdgePtrVector::iterator itend = mutEdgeVector.end();
    while(it != itend){
         EdgePtr tmpEdge=*it;
         dTotalMutEdgeLen+=tmpEdge->getLength();
         it++;
    }
    //select the lowest edge.
    //cerr<<"mutEdgeVector.size:"<<mutEdgeVector.size()<<endl;
    //cerr<<"dTotalMutEdgeLen:"<<dTotalMutEdgeLen<<endl;
    double testHeight=100.0;
    EdgePtr curMutEdge;
    bool found = false;       
    double dRandomSpot = pRandNumGenerator->unifRV() * dTotalMutEdgeLen;
    //cerr<<"begin to find edge from mutation edge vector"<<endl;
    unsigned int counter=0;
    double dRunningLength = 0.0;
    EdgePtrVector::iterator itMutEdge = mutEdgeVector.begin();
    while(!found && counter<mutEdgeVector.size()){
        curMutEdge = *itMutEdge;
        if (dRandomSpot<dRunningLength+curMutEdge->getLength()){
            dSplitPoint = curMutEdge->getBottomNodeRef()->getHeight()+
            (dRandomSpot - dRunningLength);
            found = true;
        }else{
            dRunningLength+=curMutEdge->getLength();
        }
        ++counter;
        ++itMutEdge;
    }
    if (!found) throw("Exception: can not find mutation edge!");    
    //cerr<<"end of selectRandomEdgeWithMutation "<<dSplitPoint<<endl;
    return curMutEdge;
    
};
//wy added 
double GraphBuilder::TopAcceptEdgeHeight(NodePtr Node,NodePtr pRoot, SamplesBySite& samples, double dStartPos )
{
    EdgePtrVector::iterator it = pEdgeVectorInTree->begin();
    EdgePtrVector::iterator itEnd = pEdgeVectorInTree->end();
    EdgePtr curEdge;
    double dHeight = 0.0;
    if(!pMutSolver->SolveNextSite(pRoot, samples, dStartPos)){
          GeneString* pGS1 = (GeneString*)Node->getAssoData();
          GeneString* pGS2 = (GeneString*)localMRCA->getAssoData();
          if(pGS1->diff(*pGS2) == 0){
               dHeight = 100.0; 
          }else{                                             
              while(it != itEnd){
                   curEdge = *it;
                   if (!curEdge->bDeleted){
                          if(curEdge->iGraphIteration == this->iCurrentTreeId){
                             GeneString* pGS1 = (GeneString*)Node->getAssoData();
                             GeneString* pGS2 = (GeneString*)curEdge->getBottomNodeRef()->getAssoData();
                             if(pGS1->diff(*pGS2) == 0){
                                if(curEdge->getTopNodeRef()->getHeight() > dHeight)
                                    dHeight = curEdge->getTopNodeRef()->getHeight();              
                             }                            
                           }
                   }
                   it++; 
             }
          }                                   
    }else{
          GeneString* pGS1 = (GeneString*)Node->getAssoData();
          GeneString* pGS2 = (GeneString*)localMRCA->getAssoData();
          if(pGS1->diff(*pGS2) == 0){
               dHeight = 100.0; 
          }else{                                       
                if(isMutEdge(Node)){
                    dHeight = 100.0;                   
                }else{
                    while(it != itEnd){
                       curEdge = *it;
                       if (!curEdge->bDeleted){
                          if(curEdge->iGraphIteration == this->iCurrentTreeId){
                             GeneString* pGS1 = (GeneString*)Node->getAssoData();
                             GeneString* pGS2 = (GeneString*)curEdge->getBottomNodeRef()->getAssoData();
                             if(pGS1->diff(*pGS2) == 0){
                                if(curEdge->getTopNodeRef()->getHeight() > dHeight)
                                    dHeight = curEdge->getTopNodeRef()->getHeight();                      
                             }                            
                          }
                       }
                       it++;
                    }
                } 
          }
    }                
    return dHeight;                                        
};

NodePtr GraphBuilder::getXoverBtmNode(EdgePtr Edge)
{
  NodePtr Node = Edge->getBottomNodeRef();
  EdgePtr tmpEdge;
     //cerr<<"before if"<<endl;
  while(Node->getBottomEdgeByIndex(0)==NULL
         || Node->getBottomEdgeByIndex(1)==NULL
         || Node->getBottomEdgeByIndex(0)->iGraphIteration!=this->iCurrentTreeId
         || Node->getBottomEdgeByIndex(1)->iGraphIteration!=this->iCurrentTreeId
         || Node->getType() != Node::SAMPLE)
      {
        //select the top edge	whose iGraphIteration is big.                        
        EdgePtr edge0 = Node->getBottomEdgeByIndex(0);
        EdgePtr edge1 = Node->getBottomEdgeByIndex(1);                    
        int iter0=-1,iter1=-1;
        if (edge0!=NULL) iter0=edge0->iGraphIteration;
        if (edge1!=NULL) iter1=edge1->iGraphIteration;
        if (iter0==-1 && iter1==-1)
          break;
        if(iter0==this->iCurrentTreeId){
              tmpEdge = edge0;
        }else if(iter1==this->iCurrentTreeId){
              tmpEdge = edge1; 
        }else{
              tmpEdge = (iter0>iter1)? edge0:edge1;  
        }                   
        Node = tmpEdge->getBottomNodeRef(); 
      }
     return Node;     
}

NodePtr GraphBuilder:: TracebacktoCurrentTree(EdgePtr Edge)
{       
       NodePtr Node = Edge->getTopNodeRef();
       NodePtr resultNode;
       EdgePtr tmpEdge;
         //cerr<<"before if"<<endl;
      if(Node->getBottomEdgeByIndex(0)!=NULL
           && Node->getBottomEdgeByIndex(1)!=NULL)
       {//cerr<<"in if"<<endl;
        if(Node->getBottomEdgeByIndex(0)->iGraphIteration == this->iCurrentTreeId
           && Node->getBottomEdgeByIndex(1)->iGraphIteration == this->iCurrentTreeId)
         {//cerr<<"inin if"<<endl;
           resultNode = Edge->getBottomNodeRef();
         }
       }
       //cerr<<"end if"<<endl;
      while(Node->getBottomEdgeByIndex(0)==NULL
             || Node->getBottomEdgeByIndex(1)==NULL
             || Node->getBottomEdgeByIndex(0)->iGraphIteration!=this->iCurrentTreeId
             || Node->getBottomEdgeByIndex(1)->iGraphIteration!=this->iCurrentTreeId)
          {
            resultNode = Node;
            //select the top edge	whose iGraphIteration is big.                        
            EdgePtr edge0 = Node->getTopEdgeByIndex(0);
            EdgePtr edge1 = Node->getTopEdgeByIndex(1);
                        
            int iter0=-1,iter1=-1;
            if (edge0!=NULL) iter0=edge0->iGraphIteration;
            if (edge1!=NULL) iter1=edge1->iGraphIteration;
            if (iter0==-1 && iter1==-1)
            {
              resultNode = localMRCA;
              break;
             }
             if(iter0==this->iCurrentTreeId){
                  tmpEdge = edge0;
             }else if(iter1==this->iCurrentTreeId){
                  tmpEdge = edge1; 
             }else{ 
                  tmpEdge = (iter0>iter1)? edge0:edge1;  
             }                      
             Node = tmpEdge->getTopNodeRef(); 
          }
         return resultNode;
        
} ; 
/*
erase the edge whose bottom node have different GeneString with XoverEdgeTopNode
*/
void GraphBuilder:: eraseEdgeVector(EdgePtrVector& EdgeVector, NodePtr XoverEdgeTopNode)
{
           EdgePtrVector::iterator it = EdgeVector.begin();
           EdgePtr curEdge;
           NodePtr coalEdgeTopNode;
           //cerr<<"EdgeVector.size:"<<EdgeVector.size()<<endl;
           while(it != EdgeVector.end()){
             curEdge = *it;
             coalEdgeTopNode = TracebacktoCurrentTree(curEdge);
             GeneString* pGS1 = (GeneString*)XoverEdgeTopNode->getAssoData();
             GeneString* pGS3 = (GeneString*)coalEdgeTopNode->getAssoData();
             if(pGS1->diff(*pGS3) != 0)
             {
               it = EdgeVector.erase(it);
             }else{                                 
               it++;
             }    
           }  
           //cerr<<"EdgeVector.size:"<<EdgeVector.size()<<endl;        
}

/*  
if the top edge of XoverEdgeTopNode is mutation edge.
*/
bool GraphBuilder:: isMutEdge(NodePtr XoverEdgeTopNode)
{
        NodePtr testNode = XoverEdgeTopNode;
        EdgePtr tmpEdge,edge0 = testNode->getTopEdgeByIndex(0),edge1 = testNode->getTopEdgeByIndex(1);               
        int iter0=-1,iter1=-1;
        if (edge0!=NULL) iter0=edge0->iGraphIteration;
        if (edge1!=NULL) iter1=edge1->iGraphIteration;
        if (iter0==-1 && iter1==-1)
        {
           cerr<<"the XoverEdge is higher than MRCA"<<endl;
        }else{
             if(iter0==this->iCurrentTreeId){
                  tmpEdge = edge0;
             }else if(iter1==this->iCurrentTreeId){
                  tmpEdge = edge1; 
             }else{ 
                  tmpEdge = (iter0>iter1)? edge0:edge1;  
             }                          
           testNode = tmpEdge->getTopNodeRef();
        };
        if(testNode != localMRCA){
            GeneString* pGS1 = (GeneString*)XoverEdgeTopNode->getAssoData();
            GeneString* pGS2 = (GeneString*)testNode->getAssoData();
            //check if the XoverEdge is the only MutEdge
            //if yes, accept all the Edges, if not check
            if(pGS1->diff(*pGS2) != 0)
            {return true;
            }else{
               return false;
            }
        }else{
            return true;     
        }
        
}

EdgePtr GraphBuilder:: getAcceptableEdgeToCoalesce(EdgePtr & coalescingEdge,EdgePtr XoverEdge,double dHeight,NodePtr pRoot, 
SamplesBySite& samples, double dStartPos)
{
    if (this->bEndGeneConversion){
          return this->gcOldEdge;
    }
    //cerr<<"in getAcceptableEdgeToCoalesce"<<endl;
    EdgePtrVector tmpEdgeVector;
    int iPopulation = coalescingEdge->getBottomNodeRef()->getPopulation();
    EdgePtrVector  EdgeVector;
    EdgePtrVector::iterator it = this->pEdgeVectorByPop->at(iPopulation).begin();
    EdgePtrVector::iterator itEnd = this->pEdgeVectorByPop->at(iPopulation).end();
    EdgePtr curEdge;
    //cerr<<"before Traceback"<<endl;
    NodePtr coalEdgeTopNode,XoverEdgeTopNode = TracebacktoCurrentTree(XoverEdge);
    double XoverTopHeight;
    //initialize
    //cerr<<"before initialize"<<endl;
    while(it != itEnd){
        curEdge = *it;
        if (curEdge->getBottomNodeRef()->getHeight()<dHeight
             && curEdge->getTopNodeRef()->getHeight()>dHeight
             && !curEdge->bDeleted){
               coalEdgeTopNode = TracebacktoCurrentTree(curEdge);
               if(coalEdgeTopNode != XoverEdgeTopNode){
                 EdgeVector.push_back(curEdge);        
               }                                      
         }
        it++;
    }  
    
    if(EdgeVector.empty()){
         throw "initial Accpetalbe edge vector is empty!!!";                                               
      };
    tmpEdgeVector.assign(EdgeVector.begin(),EdgeVector.end());
    
    //dataset of the AcceptableEdgeToCoalesce
    if(pMutSolver->findNextSolveFailPos(pRoot,samples,dStartPos)==1.0)
    {
        //cerr<<"next solve failure point is 1.0"<<endl;
        if(pMutSolver->SolveNextSite(pRoot, samples, dStartPos))
        {  
            if(!isMutEdge(XoverEdgeTopNode)) 
            {
               eraseEdgeVector(EdgeVector, XoverEdgeTopNode);
            }
        }else{
            cerr<<"solveNextSite failed while findNextSolveFailPos exceed 1!!!"<<endl;
            throw "solveNextSite failed while findNextSolveFailPos exceed 1!!!";
        }
    }else{ 
          //cerr<<"before for(int i=0;i<nSiteNum;i++)"<<endl;
          int nSiteNum = samples.getSiteNum();
          for(int i=0;i<nSiteNum;i++)
          {
              Site& site = samples.getSite(i);
              if(site.getPos() < dStartPos)
               continue;      
   //found the site further than dStartPos;  
              bool bSolveRes = pMutSolver->SolveMutation(pRoot,samples,i); 
              if(bSolveRes){ 
                  if(!isMutEdge(XoverEdgeTopNode))  
                  {
                    //cerr<<"before if(!isMutEdge(XoverEdgeTopNode))"<<endl;  
                     eraseEdgeVector(EdgeVector, XoverEdgeTopNode);
                  }//end if(pGS1->diff(*pGS2) == 0) 
                   continue;//for(int i=0;i<nSiteNum;i++)
              }else{//the next solve failPos end if(bSolveRes)
                  eraseEdgeVector(EdgeVector, XoverEdgeTopNode);
                  break;   
              }         
           }//end  for(int i=0;i<nSiteNum;i++) 
     
       //check if there is AcceptableEdgeToCoalesce according to the nextFailPos 
         if(EdgeVector.empty())
         {
              EdgeVector.assign(tmpEdgeVector.begin(),tmpEdgeVector.end());
              if(pMutSolver->SolveNextSite(pRoot, samples, dStartPos))
              {  if(!isMutEdge(XoverEdgeTopNode))  
                {
                   eraseEdgeVector(EdgeVector, XoverEdgeTopNode);
                }
              }else{
                cerr<<"Err:SolveNextSite fail"<<endl;
                eraseEdgeVector(EdgeVector, XoverEdgeTopNode);
              }             
         }//end if(EdgeVector.empty())
      }//end if(pMutSolver->findNextSolveFailPos(pRoot,samples,curPos)==1)
      
      
      if(EdgeVector.empty()){
         throw "Accpetalbe edge vector is empty!!!";                                               
      };
 //return the edge accoring to  getRandomEdgeToCoalesce() 

      //cerr<<"before select"<<endl;
      EdgeIndexQueue & pVectorIndicesToRecycle = this->pVectorIndicesToRecycle->at(iPopulation);   
      EdgePtrVector & pEdgeVector = this->pEdgeVectorByPop->at(iPopulation);
        bool found = false;
        int iUnifPick =-1;
        int id;
        while(!found){
            iUnifPick = static_cast<int>(pRandNumGenerator->unifRV()*EdgeVector.size());
            EdgePtr resultEdge = EdgeVector[iUnifPick];
            EdgePtrVector::iterator iter = find(pEdgeVector.begin(),pEdgeVector.end(),resultEdge);
            if(iter == pEdgeVector.end()){
                 throw"cant fint the accept edge in ARG";
            }
            for(int i=0;i<pEdgeVector.size();i++){
                if(resultEdge == pEdgeVector[i]){
                     id=i;
                     break;                      
                }            
            }
            if (resultEdge->bDeleted){
                 if (!resultEdge->bInQueue){
                    pVectorIndicesToRecycle.push(id);
                    resultEdge->bInQueue = true;
                 }
            }else{
                 found = true;
            }
        }
       
    EdgePtr & edge=pEdgeVector[id];
    //cerr<<"Edge to coalesce: "<<edge->getBottomNodeRef()->getHeight()<<" to "<<edge->getTopNodeRef()->getHeight()<<endl;
    return edge; 
};  

//wy added end    
//llf added end
void GraphBuilder::invokeRecombination(GeneConversionPtr & geneConversionPtr,NodePtr & preXoverNode){
    double dSplitPoint=0.0;
    double dRandomSpot = pRandNumGenerator->unifRV() * dLastTreeLength;
    //llf need to check if current tree can solve mutation for next site after curPos.
    //if yes, still use getRandomEdgeOnTree.
    //if no, need to select xnode on edge with mutation(the edge top/bottom node must be localtree node, node with old bottom edge need to be ignored).
//    EdgePtr selectedXoverEdge = getRandomEdgeOnTree(dSplitPoint,dRandomSpot);
    EdgePtr selectedXoverEdge;
    /*
    if(pMutSolver->SolveNextSite(localMRCA, *pSample, this->dCurPos)){
      selectedXoverEdge = getRandomEdgeOnTree(dSplitPoint,dRandomSpot);                                                                            
    }else{
      selectedXoverEdge = selectRandomEdgeWithMutation(dSplitPoint);              
    };
    */
    //wy added
      double curPos = this->dCurPos;
      double runNextSolveFailPos = pMutSolver->findNextSolveFailPos(localMRCA,*pSample,curPos);
      if(runNextSolveFailPos == 1.0){
         if(pMutSolver->SolveNextSite(localMRCA, *pSample, this->dCurPos)){
            selectedXoverEdge = getRandomEdgeOnTree(dSplitPoint,dRandomSpot);                                                                            
          }else{
            throw"nextSolveFailPos is 1, but solve failed";
          }
       }else{
        selectedXoverEdge = selectRandomEdgeWithMutation(dSplitPoint);
       }
       if(pMutSolver->SolveNextSite(localMRCA, *pSample, this->dCurPos)
        && preXoverNode != NULL && getXoverBtmNode(selectedXoverEdge) == preXoverNode)
       {      //cerr<<"choose the same XoverEdge two times in a row "<<endl;
              selectedXoverEdge = getRandomEdgeOnTree(dSplitPoint,dRandomSpot);
       }
       preXoverNode=getXoverBtmNode(selectedXoverEdge);
    //wy added end
   
    //llf added end.
    
    xOverNode = NodePtr(new Node(Node::XOVER,
    selectedXoverEdge->getBottomNodeRef()->getPopulation(),dSplitPoint));
    // save old edge if gene conversion
    if (this->bBeginGeneConversion) geneConversionPtr->xOverNode = xOverNode;

    if(pConfig->bDebug){
         cerr<<"Adding xover point at "<<dSplitPoint<<
        " for edge "<<selectedXoverEdge->getBottomNodeRef()->getHeight()<<
        " to "<<selectedXoverEdge->getTopNodeRef()->getHeight()<<
        " of population "<<selectedXoverEdge->getBottomNodeRef()->getPopulation()<<endl;
    }
    //cerr<<"before new EventPtr"<<endl;
    EventPtr pXoverEventWrapper = EventPtr(new XoverEvent
    (Event::XOVER,dSplitPoint,xOverNode->getPopulation()));
    // insert the xover node at the selected edge
    insertNodeInEdge(xOverNode,selectedXoverEdge);

    // mark the edge above the xover.  this is the old coalescing line to prune.
    // set up a coalescing line that is visible to the
    // event building method
    NodePtr nodeAboveXover = NodePtr(new Node
    (Node::QUERY,xOverNode->getPopulation(),Node::MAX_HEIGHT));
    coalescingEdge = EdgePtr(new Edge(nodeAboveXover,xOverNode));
    nodeAboveXover->addNewEdge(Node::BOTTOM_EDGE,coalescingEdge);
    xOverNode->addNewEdge(Node::TOP_EDGE,coalescingEdge);
    // similarly, set up a line that runs straight up from the grandMRCA
    // in case migration events need to be inserted above the grandMRCA.
    NodePtr nodeAboveOrigin = NodePtr(new Node
    (Node::QUERY,grandMRCA->getPopulation(),Node::MAX_HEIGHT));
    originExtension = EdgePtr(new Edge(nodeAboveOrigin,grandMRCA));
    nodeAboveOrigin->addNewEdge(Node::BOTTOM_EDGE,originExtension);
    grandMRCA->addNewEdge(Node::TOP_EDGE,originExtension);

    // now initialize the parameters that will be sent to the event traversal routine
    EventPtr newCoalEvent;
    bool bNewOrigin = false;
    EdgePtr selectedCoalEdge;
  //wy added
  //Get a right coalescene node height directly.
  traverseEvents(true,xOverNode,newCoalEvent);
  dSplitPoint = newCoalEvent->getTime();
  
  //cerr<<"before splitpoint"<<endl;
  double TopAEHeight = TopAcceptEdgeHeight(TracebacktoCurrentTree(selectedXoverEdge),localMRCA, *pSample, this->dCurPos);
  //cerr<<"TopAEHeight : "<<TopAEHeight<<endl;
  while(dSplitPoint > TopAEHeight){
       traverseEvents(true,xOverNode,newCoalEvent);
       dSplitPoint = newCoalEvent->getTime();                
   }        
  
    //cerr<<"XoverHeight : "<<TracebacktoCurrentTree(selectedXoverEdge)->getHeight()<<endl;
    //cerr<<"dSplitPoint : "<<dSplitPoint<<endl;
    
    if (dSplitPoint<this->grandMRCA->getHeight()){
            //cerr<<"before getAcceptableEdgeToCoalesce"<<endl;
            selectedCoalEdge = getAcceptableEdgeToCoalesce(coalescingEdge,selectedXoverEdge, dSplitPoint, localMRCA, *pSample, this->dCurPos);
            //cerr<<"end getAcceptableEdgeToCoalesce"<<endl;
    }else{
        bNewOrigin = true;
    }
    
    //wy added end                   
    NodePtr coalNode;
    // save the previous grand MRCA
    NodePtr oldGrandMRCA = grandMRCA;
    // discard the grandMRCA extending edge above the grandMRCA
    grandMRCA = originExtension->getBottomNodeRef();
    grandMRCA->clearTopEdges();
    if (bNewOrigin){
        coalNode = NodePtr(new Node(Node::COAL,
        grandMRCA->getPopulation(),dSplitPoint));
        if(pConfig->bDebug) cerr<<"Creating new grand ancestor at "<<
        coalNode->getHeight()<<endl;
        #ifdef DIAG
        if (grandMRCA->getPopulation()!=coalNode->getPopulation()){
            throw "Old grandMRCA and new grandMRCA don't match in pop\n";
        }
        #endif
        EdgePtr shortEdge = EdgePtr(new Edge(coalNode,grandMRCA));
        shortEdge->iGraphIteration=iGraphIteration;
        addEdge(shortEdge);
        grandMRCA->addNewEdge(Node::TOP_EDGE,shortEdge);
        grandMRCA = coalNode;
        grandMRCA->addNewEdge(Node::BOTTOM_EDGE,shortEdge);
    }else{
        coalNode = NodePtr(new Node(
        Node::COAL,coalescingEdge->getBottomNodeRef()->getPopulation(),
        dSplitPoint));
        insertNodeInEdge(coalNode,selectedCoalEdge);
    }
    // set the variable proposed grandMRCA so that marking current tree can reach at least this
    // height
    localMRCA = coalNode;
    coalNode->setEvent(newCoalEvent);
    EdgePtr coalEdgeCopy = coalescingEdge;
    coalescingEdge = EdgePtr(new Edge(coalNode,coalescingEdge->getBottomNodeRef()));
    coalescingEdge->iGraphIteration = coalEdgeCopy->iGraphIteration;
    coalescingEdge->getBottomNodeRef()->replaceOldWithNewEdge(Node::TOP_EDGE,
    coalEdgeCopy,coalescingEdge);
    coalescingEdge->getTopNodeRef()->addNewEdge(Node::BOTTOM_EDGE,
    coalescingEdge);
    addEdge(coalescingEdge);
    coalescingEdge->iGraphIteration=iGraphIteration;
    EdgePtr edgeBelowXover = xOverNode->getBottomEdgeByIndex(0);
    markEdgesBelow(edgeBelowXover);
}

void GraphBuilder::markCurrentTree(){
    iTotalTreeEdges = 0;
    unsigned int iTotalSamples=pConfig->iSampleSize;
    for(unsigned int i=0;i<iTotalSamples;++i){
        NodePtr & node = pSampleNodeArray[i];
        pTreeEdgesToCoalesceArray[i]=node->getTopEdgeByIndex(0);
    }
    unsigned int iIndex = 0;
    int iIterations = 0;
    bool bFirstSample = true;
    while(iIndex<iTotalSamples){
        EdgePtr & curEdge = pTreeEdgesToCoalesceArray[iIndex];
        if (markEdgesAbove(bFirstSample,true,curEdge,iIndex)){
            ++iIndex;
        }else{
            // start over
            iIndex = 0;
        }
        bFirstSample = false;
        ++iIterations;
    }
    #ifdef DIAG
    if (localMRCA->getBottomEdgeByIndex(0)->iGraphIteration!=
    localMRCA->getBottomEdgeByIndex(1)->iGraphIteration){
        printDataStructures();
        cerr<<"Proposed grandMRCA at :"<<localMRCA->getHeight()<<endl;
        throw "Proposed grandMRCA's edges' histories don't match";
    }
    #endif
}



void GraphBuilder::traverseEvents(bool bBuildFromEventList,
NodePtr & xOverNode, EventPtr & newCoalEvent){
    short int iRequestedPop = -1;
    short int iOriginPop = -1;
    double dXoverHeight = 0.;
    if (bBuildFromEventList){
        iRequestedPop = xOverNode->getPopulation();
        iOriginPop = grandMRCA->getPopulation();
        dXoverHeight = xOverNode->getHeight();
    }
    // copy stuff over from Configuration object
    int iTotalPops = pConfig->iTotalPops;
    // make a copy of the population information to the dynamic vector
    PopVector pPopList = pConfig->pPopList;
    // make a copy of the the migration matrix over to our dynamic 2-d vector
    if (pConfig->bMigrationChangeEventDefined)
        dMigrationMatrix = pConfig->dMigrationMatrix;

    // set up pile of coalesced nodes for building the prior tree
    NodePtrSet * pCoalescedNodes = NULL;
    if (!bBuildFromEventList){
        // store coalesced nodes in a temp vector
        //pCoalescedNodes = new NodePtrList();
        pCoalescedNodes = new NodePtrSet();
        // set up the node list
        int iCounter = 0,iId=0;
        for (int i=0;i<iTotalPops;++i){
            int iSampleSize = pConfig->pPopList[i].getChrSampled();
            for (int j=0;j<iSampleSize;++j){
                NodePtr newNode = NodePtr(new SampleNode
                (i,iId));
                //llf added to set genestring to sample node.
                //(i,iId++));
                char cFirstSiteValue = pSample->getSiteValue(0,iId);
                //GeneString *pGS = new GeneString(&cFirstSiteValue,1);
                newNode->setCData(cFirstSiteValue);  //the GeneString is release in Node::~Node.
                cout<<"setcdata for sample "<<j<<" : "<<cFirstSiteValue<<endl;
                nValueCnt[(int)cFirstSiteValue - (int)('0')]++;           
                //llf added end.
                this->pSampleNodeArray[iCounter++]=newNode;
                pCoalescedNodes->insert(newNode);
                iId++;
            }
        }
    }

    // remove any leading events that are marked for deletion.
    EventPtrList::iterator currentEventIt = pEventList->begin();
    EventPtrList::iterator lastEventIt = pEventList->end();
    EventPtr pNextNewEvent;
    if (currentEventIt!=lastEventIt){
        pNextNewEvent = *currentEventIt;
        while (pNextNewEvent->bMarkedForDelete){
            currentEventIt = pEventList->erase(currentEventIt);
            pNextNewEvent = *currentEventIt;
        }
    }

    unsigned int iChromCounter = bBuildFromEventList?0:pCoalescedNodes->size();
    bool bDoneBuild = false,bIsEvent = false,
    bUserEventAvailable = false,bAboveOrigin = false,
    bAboveXoverHeight = false,bProposeNewCoal = false;
    short int iCoalescingPop = -1;
    unsigned long int iCoalRate = 0;
    double dTime = 0.0,dMigration = 0.0,dLastTime = 0.0,dWaitTime=0.;
    while (!bDoneBuild){
//        for(int pop=0; pop<iTotalPops ; ++pop) {      //coalescent
//          int iChrSampled=(pPopList[pop].getPopSize()>0.)?pPopList[pop].getChrSampled():0;
//          cerr<<"At time "<<dTime<<", pop "<<pop<<" has chr sampled "<<iChrSampled<<endl;
//          cerr<<"Requested pop is "<<iRequestedPop<<endl;
//        }
        int iEventType = -1;
        if (currentEventIt!=lastEventIt){
            pNextNewEvent = *currentEventIt;
            bUserEventAvailable = true;
            if (bBuildFromEventList){
                //cerr<<"Xover height "<<dXoverHeight<<endl;
                if (dXoverHeight>dLastTime &&
                dXoverHeight<=pNextNewEvent->getTime()){
                    EventPtr eventWrapper = EventPtr(new XoverEvent(
                    Event::XOVER,dXoverHeight,iRequestedPop));
                    xOverNode->setEvent(eventWrapper);
                    pEventList->insert(currentEventIt,eventWrapper);
                    pNextNewEvent = eventWrapper;
                    currentEventIt--;
                }
            }
        }else{
            bUserEventAvailable = false;
        }
        if (bBuildFromEventList){
            if (dTime==dXoverHeight){
                bAboveXoverHeight = true;
                bProposeNewCoal = true;
            }
            if (dTime==grandMRCA->getHeight()){
                bAboveOrigin = true;
            }
        }
        if (!bBuildFromEventList || bAboveXoverHeight){
            bIsEvent = false;
            double dProposedWaitTime = 0.;
            // BEGIN COALESCENT SECTION
            if(bProposeNewCoal ||!bBuildFromEventList){
                // attempt a coalescent event
                for(int pop=0; pop<iTotalPops ; ++pop) {      //coalescent
                    int iChrSampled=(pPopList[pop].getPopSize()>0.)?pPopList[pop].getChrSampled():0;
                    if (bProposeNewCoal){
                      iCoalRate = iRequestedPop==pop?(iChrSampled-1)*2:0;
                    }else{
                      iCoalRate =  iChrSampled*(iChrSampled-1);
                    }
                    if( iCoalRate > 0 ) {
                        if (pPopList[pop].getGrowthAlpha() == 0. ){
                            dProposedWaitTime = pRandNumGenerator->expRV(
                            iCoalRate/pPopList[pop].getPopSize());
                        } else {  // support pop growth
                            double dRandomVar=pRandNumGenerator->unifRV();
                            double dCoalRateAlpha =
                            1. - pPopList[pop].getGrowthAlpha()*
                            pPopList[pop].getPopSize()*
                            exp(-pPopList[pop].getGrowthAlpha()*
                            (dTime - pPopList[pop].getLastTime() ) )*
                            log(dRandomVar) / iCoalRate;
                            if( dCoalRateAlpha > 0.0 ){
                                // if  <= 0,  no coalescent within interval
                                dProposedWaitTime = log(dCoalRateAlpha) /
                                pPopList[pop].getGrowthAlpha();
                            }
                        }
                        if (!bIsEvent||dProposedWaitTime<dWaitTime) {
                            iCoalescingPop = pop;
                            dWaitTime = dProposedWaitTime;
                        }
                        iEventType = Event::NEW_COAL;
                        bIsEvent = true;
                        if (this->bEndGeneConversion){
                            dWaitTime = 0.0;
                        }
                    }
                }
            }
            // END COALESCENT SECTION
            // BEGIN MIGRATION SECTION
            dMigration = 0.0;
            if (bBuildFromEventList){
                if (bAboveOrigin){
                    dMigration = dMigrationMatrix[iRequestedPop][iRequestedPop]+
                    dMigrationMatrix[iOriginPop][iOriginPop];
                }else{
                    dMigration = dMigrationMatrix[iRequestedPop][iRequestedPop];
                }
            }else{
                for(int i=0; i<iTotalPops; ++i){
                    dMigration +=
                    pPopList[i].getChrSampled()*dMigrationMatrix[i][i];
                }
            }
                // Attempt a migration event
            if(dMigration > 0.0 ) {          // migration
                dProposedWaitTime = pRandNumGenerator->expRV(dMigration);
                if(!bIsEvent || dProposedWaitTime < dWaitTime){
                    dWaitTime = dProposedWaitTime;
                    iEventType = Event::NEW_MIGRATION;
                    bIsEvent = true;
                }
            }else{
                #ifdef DIAG
                if (dMigration<0.0)
                    throw "Negative migration. Program exiting";
                #endif
                if(iTotalPops>1 && !bUserEventAvailable){
                        int iPops = 0;
                        for(int j=0; j<iTotalPops; ++j) {
                            if( pPopList[j].getChrSampled() > 0 ) ++iPops;
                        }
                        if( iPops > 1 ) {
                            throw "Infinite coalescent time. No migration";
                        }
                }
            }
            // END MIGRATION SECTION
        }
        // Now try one of the user invoked events
        if (bUserEventAvailable &&
        (!bIsEvent || dTime+dWaitTime > pNextNewEvent->getTime())
        ){
            dTime = pNextNewEvent->getTime();
            short int eventType =  pNextNewEvent->getType();
            if (eventType==Event::PAST_COAL){
                CoalEvent * pExistingCoalEvent =
                static_cast<CoalEvent *>(pNextNewEvent.get());
                iCoalescingPop = pExistingCoalEvent->getPopulation();
                pPopList[iCoalescingPop].changeChrSampled(-1);
            }else if (eventType==Event::PAST_MIGRATION){
                //cerr<<"Past migration event at time "<<dTime<<endl;
                MigrationEvent * pExistingMigEvent =
                static_cast<MigrationEvent *>(pNextNewEvent.get());
                short int iSourcePop = pExistingMigEvent->getPopMigratedFrom();
                short int iDestPop = pExistingMigEvent->getPopMigratedTo();

                if (iDestPop==iTotalPops){
                   // we need to split the populations
                   Population newPop;
                   pPopList.push_back(newPop);
                   iTotalPops = pPopList.size();
                   vector <double> newRow(iTotalPops);
                   for (int j=0;j<iTotalPops;++j)
                       newRow[j]=0.0;
                   dMigrationMatrix.push_back(newRow);
                   for (int j=0;j<iTotalPops;++j)
                      dMigrationMatrix[j].push_back(0.0);
                }

                pPopList[iSourcePop].changeChrSampled(-1);
                pPopList[iDestPop].changeChrSampled(1);
               
            }else if (eventType==Event::XOVER){ //recombination
                XoverEvent * pXoverEvent =
                static_cast<XoverEvent *>(pNextNewEvent.get());
                pPopList[pXoverEvent->getPopulation()].changeChrSampled(1);
            }else if (eventType==Event::MIGRATION_MATRIX_RATE){
                MigrationRateMatrixEvent* migRateMatrixEvent = static_cast
                <MigrationRateMatrixEvent*>(pNextNewEvent.get());
                dMigrationMatrix = migRateMatrixEvent->getMigrationMatrix();
            }else if (eventType==Event::MIGRATION_RATE){
                MigrationRateEvent* migRateEvent = static_cast
                <MigrationRateEvent*>(pNextNewEvent.get());
                short int  iSourcePop = migRateEvent->getSourcePop();
                short int iDestPop = migRateEvent->getDestPop();
                double dMigRate = migRateEvent->getRate();
                dMigrationMatrix[iSourcePop][iSourcePop]+=
                dMigRate-dMigrationMatrix[iSourcePop][iDestPop];
                dMigrationMatrix[iSourcePop][iDestPop] = dMigRate;
            }else if (eventType==Event::GLOBAL_MIGRATIONRATE){
                GenericEvent * pGeneriEvent =
                static_cast<GenericEvent *>(pNextNewEvent.get());
                for(short unsigned int i=0;i<iTotalPops;++i){
                    for(short unsigned int j=0;j<iTotalPops;++j){
                        dMigrationMatrix[i][j] =
                        pGeneriEvent->getParamValue()/(iTotalPops-1);
                    }
                }
                for(short unsigned int i=0;i<dMigrationMatrix.size();++i){
                    dMigrationMatrix[i][i] =
                        pGeneriEvent->getParamValue();
                }
            }else if (eventType==Event::GROWTH){
                PopSizeChangeEvent * growthEvent = static_cast
                <PopSizeChangeEvent *>(pNextNewEvent.get());
                short int pop = growthEvent->getPopulationIndex();
                pPopList[pop].setPopSize(pPopList[pop].getPopSize()*
                exp( - pPopList[pop].getGrowthAlpha()*(
                dTime-pPopList[pop].getLastTime()) ))  ;
                pPopList[pop].setGrowthAlpha(growthEvent->getPopChangeParam());
                pPopList[pop].setLastTime(dTime);
            }else if (eventType==Event::POPSIZE){
                PopSizeChangeEvent * growthEvent = static_cast
                <PopSizeChangeEvent *>(pNextNewEvent.get());
                short int pop = growthEvent->getPopulationIndex();
                pPopList[pop].setPopSize(growthEvent->getPopChangeParam());
                pPopList[pop].setGrowthAlpha(0);
            }else if (eventType==Event::POPJOIN){
                // merge pop i into pop j  (join)
                PopJoinEvent * joinEvent = static_cast
                <PopJoinEvent *>(pNextNewEvent.get());
                short int iSourcePop = joinEvent->getSourcePop();
                short int iDestPop = joinEvent->getDestPop();

                if (iDestPop==iTotalPops){
                   // we need to split the populations
                   Population newPop;
                   pPopList.push_back(newPop);
                   iTotalPops = pPopList.size();
                   vector <double> newRow(iTotalPops);
                   for (int j=0;j<iTotalPops;++j)
                       newRow[j]=0.0;
                   dMigrationMatrix.push_back(newRow);
                   for (int j=0;j<iTotalPops;++j)
                      dMigrationMatrix[j].push_back(0.0);
                }

                if (!bBuildFromEventList){
                    NodePtrList nodesToMigrate;
                    for (NodePtrSet::iterator it=pCoalescedNodes->begin();
                    it!=pCoalescedNodes->end();++it){
                        NodePtr node = *it;
                        if (node->getPopulation()==iSourcePop){
                            nodesToMigrate.push_back(node);
                        }
                    }
                    for (NodePtrList::iterator it=nodesToMigrate.begin();
                    it!=nodesToMigrate.end();++it){
                        NodePtr node = *it;
                        NodePtr childNode = node;
                        NodePtr parentNode =
                        NodePtr(new Node(Node::MIGRATION,
                        iDestPop,dTime));
                        EdgePtr newEdge = EdgePtr(
                        new Edge(parentNode,childNode));
                        this->addEdge(newEdge);
                        parentNode->addNewEdge(Node::BOTTOM_EDGE,newEdge);
                        childNode->addNewEdge(Node::TOP_EDGE,newEdge);
                        pCoalescedNodes->erase(childNode);
                        pCoalescedNodes->insert(parentNode);
                        // store this new event
                        EventPtr eventWrapper = EventPtr(new MigrationEvent(
                        Event::PAST_MIGRATION,dTime,
                        iDestPop,iSourcePop));
                        parentNode->setEvent(eventWrapper);
                        pEventList->insert(currentEventIt,eventWrapper);
                    }
                }else{
                    if (pConfig->bDebug) cerr<<"At time: "<<dTime<<endl;
                    if (iRequestedPop==iSourcePop &&
                    coalescingEdge->getBottomNodeRef()->getHeight() < dTime &&
                    dTime < coalescingEdge->getTopNodeRef()->getHeight()){
                        NodePtr migrNode = NodePtr(new Node
                        (Node::MIGRATION,iDestPop,dTime));
                        insertNodeInRunningEdge(migrNode,coalescingEdge);
                        iRequestedPop = iDestPop;
                    }
                    if (iOriginPop==iSourcePop &&
                    originExtension->getBottomNodeRef()->getHeight() < dTime &&
                    dTime < originExtension->getTopNodeRef()->getHeight()){
                        NodePtr migrNode = NodePtr(new Node
                        (Node::MIGRATION,iDestPop,dTime));
                        insertNodeInRunningEdge(migrNode,originExtension);
                        iOriginPop = iDestPop;
                        // store this new event
                        EventPtr eventWrapper = EventPtr(new MigrationEvent(
                        Event::PAST_MIGRATION,dTime,
                        iDestPop,iSourcePop));
                        migrNode->setEvent(eventWrapper);
                        pEventList->insert(currentEventIt,eventWrapper);
                    }
                }
                pPopList[iDestPop].changeChrSampled(pPopList[iSourcePop]
                .getChrSampled()) ;
                pPopList[iSourcePop].setChrSampled(0);
            }else if (eventType==Event::POPSPLIT){
                PopSizeChangeEvent *splitEvent = static_cast
                <PopSizeChangeEvent *>(pNextNewEvent.get());
                short int iSourcePop = splitEvent->getPopulationIndex();
                short int iDestPop = iTotalPops;
                double dProportion = splitEvent->getPopChangeParam();
                if (!bBuildFromEventList){
                  Population newPop;
                  pPopList.push_back(newPop);
                  iTotalPops = pPopList.size();
                  vector <double> newRow(iTotalPops);
                  for (int j=0;j<iTotalPops;++j)
                      newRow[j]=0.0;
                  dMigrationMatrix.push_back(newRow);
                  for (int j=0;j<iTotalPops;++j)
                    dMigrationMatrix[j].push_back(0.0);
                    NodePtrList nodesToMigrate;
                    for (NodePtrSet::iterator it=pCoalescedNodes->begin();
                    it!=pCoalescedNodes->end();++it){
                        NodePtr node = *it;
                        if (node->getPopulation()==iSourcePop &&
                            pRandNumGenerator->unifRV()<dProportion){
                            nodesToMigrate.push_back(node);
                        }
                    }
                    for (NodePtrList::iterator it=nodesToMigrate.begin();
                    it!=nodesToMigrate.end();++it){
                        NodePtr node = *it;
                        NodePtr childNode = node;
                        NodePtr parentNode =
                        NodePtr(new Node(Node::MIGRATION,
                        iDestPop,dTime));
                        EdgePtr newEdge =
                        EdgePtr(new Edge(parentNode,childNode));
                        this->addEdge(newEdge);
                        parentNode->addNewEdge(Node::BOTTOM_EDGE,newEdge);
                        childNode->addNewEdge(Node::TOP_EDGE,newEdge);
                        pCoalescedNodes->erase(childNode);
                        pCoalescedNodes->insert(parentNode);
                        pPopList[iDestPop].changeChrSampled(1) ;
                        pPopList[iSourcePop].changeChrSampled(-1);
                        // store this new event
                        EventPtr eventWrapper = EventPtr(new MigrationEvent(
                        Event::PAST_MIGRATION,dTime,
                        iDestPop,iSourcePop));
                        parentNode->setEvent(eventWrapper);
                        pEventList->insert(currentEventIt,eventWrapper);
                    }
                } else{
                    iDestPop=iTotalPops-1;
                    if (iRequestedPop==iSourcePop&&
                    coalescingEdge->getBottomNodeRef()->getHeight() < dTime &&
                    dTime < coalescingEdge->getTopNodeRef()->getHeight()&&
                        pRandNumGenerator->unifRV()<dProportion){
                        NodePtr migrNode = NodePtr(new Node
                        (Node::MIGRATION,iDestPop,dTime));
                        insertNodeInRunningEdge(migrNode,coalescingEdge);
                        iRequestedPop = iDestPop;
                        pPopList[iDestPop].changeChrSampled(1) ;
                        pPopList[iSourcePop].changeChrSampled(-1);
                        // store this new event
                        EventPtr eventWrapper = EventPtr(new MigrationEvent(
                        Event::PAST_MIGRATION,dTime,
                        iDestPop,iSourcePop));
                        migrNode->setEvent(eventWrapper);
                        pEventList->insert(currentEventIt,eventWrapper);
                    }
                    if (iOriginPop&&
                    originExtension->getBottomNodeRef()->getHeight() < dTime &&
                    dTime < originExtension->getTopNodeRef()->getHeight()&&
                        pRandNumGenerator->unifRV()<dProportion){
                        NodePtr migrNode = NodePtr(new Node
                        (Node::MIGRATION,iDestPop,dTime));
                        insertNodeInRunningEdge(migrNode,originExtension);
                        iOriginPop = iDestPop;
                        pPopList[iDestPop].changeChrSampled(1) ;
                        pPopList[iSourcePop].changeChrSampled(-1);
                        // store this new event
                        EventPtr eventWrapper = EventPtr(new MigrationEvent(
                        Event::PAST_MIGRATION,dTime,
                        iDestPop,iSourcePop));
                        migrNode->setEvent(eventWrapper);
                        pEventList->insert(currentEventIt,eventWrapper);
                    }
                }
            }else if (eventType==Event::GLOBAL_POPGROWTH){
                GenericEvent * pGeneriEvent =
                static_cast<GenericEvent *>(pNextNewEvent.get());
                for(short unsigned int iPop=0;iPop<pPopList.size();++iPop){
                    Population & pop = pPopList[iPop];
                    pop.setPopSize(pop.getPopSize()*exp( - pop.getGrowthAlpha()*(dTime - pop.getLastTime())));
                    pop.setGrowthAlpha(pGeneriEvent->getParamValue());
                    pop.setLastTime(dTime);
                }
            }else if (eventType==Event::GLOBAL_POPSIZE){
                GenericEvent * pGeneriEvent =
                static_cast<GenericEvent *>(pNextNewEvent.get());
                for(short unsigned int iPop=0;iPop<pPopList.size();++iPop){
                    Population & pop = pPopList[iPop];
                    pop.setPopSize(pGeneriEvent->getParamValue());
                    pop.setGrowthAlpha(0);
                }
            }else{
                cerr<<"Found an event of type "<<pNextNewEvent->getType()
                <<endl;
                throw "Event is not implemented yet!";
            }
            ++currentEventIt;
            bool found = false;
            while(!found && currentEventIt!=lastEventIt){
                if ((*currentEventIt)->bMarkedForDelete) currentEventIt=pEventList->erase(currentEventIt);
                else found = true;
            }

        }else{
            dTime += dWaitTime;
            if(iEventType==Event::NEW_MIGRATION){
                double dRandMigr =  dMigration*pRandNumGenerator->unifRV();
                vector<int> migrantPops;
                if (!bBuildFromEventList){
                    for (NodePtrSet::iterator it = pCoalescedNodes->begin();
                    it!=pCoalescedNodes->end();++it){
                        NodePtr node = *it;
                        migrantPops.push_back(node->getPopulation());
                    }
                }else{
                    if (bAboveOrigin){
                        migrantPops.push_back(iOriginPop);
                    }
                    // In any case a candidate migrant is the new coal line
                    migrantPops.push_back(iRequestedPop);
                }
                int iTotalChrom = migrantPops.size();
                int migrant=-1,i=0;
                double dSum=0.;
                while (dRandMigr>=dSum && i<iTotalChrom){
                    dSum += dMigrationMatrix[migrantPops[i]][migrantPops[i]];
                    migrant = i ;
                    ++i;
                }
                int source_pop = -1,dest_pop = -1;
                NodePtr migrNode;
                if (!bBuildFromEventList){
                    dRandMigr = pRandNumGenerator->unifRV()*
                    dMigrationMatrix[migrantPops[migrant]][migrantPops[migrant]];
                    dSum = 0.0;
                    int i = 0;

                    while (dRandMigr>=dSum && i<iTotalPops){
                        if( i != migrantPops[migrant]){
                            dSum += dMigrationMatrix[migrantPops[migrant]][i];
                            dest_pop = i;
                        }
                        ++i;
                    }
                    NodePtrSet::iterator it = pCoalescedNodes->begin();
                    for (int i=0;i<migrant;++i) ++it;
                    NodePtr childNode = *it;
                    source_pop = childNode->getPopulation();
                    migrNode = NodePtr(new Node
                    (Node::MIGRATION,dest_pop,dTime));
                    EdgePtr newEdge = EdgePtr(new Edge(migrNode,childNode));
                    this->addEdge(newEdge);
                    migrNode->addNewEdge(Node::BOTTOM_EDGE,newEdge);
                    childNode->addNewEdge(Node::TOP_EDGE,newEdge);
                    pCoalescedNodes->erase(childNode);
                    pCoalescedNodes->insert(migrNode);
                }else{
                    if (bAboveOrigin){
                        source_pop = migrantPops[migrant];
                    }else{
                        source_pop=iRequestedPop;
                    }

                    dRandMigr = pRandNumGenerator->unifRV()*
                    dMigrationMatrix[source_pop][source_pop];
                    dSum = 0.0;
                    i = 0;

                    while (dRandMigr>=dSum && i<iTotalPops){
                        if( i != source_pop){
                            dSum += dMigrationMatrix[source_pop][i];
                            dest_pop = i;
                        }
                        ++i;
                    }
                    migrNode = NodePtr(new Node
                    (Node::MIGRATION,dest_pop,dTime));
                    if (bAboveOrigin){
                        // There are two lines above the grandMRCA and either one
                        // can have a migration event inserted.
                        switch(migrant){
                            case 0:
                                insertNodeInRunningEdge(migrNode,originExtension);
                                iOriginPop = dest_pop;
                                break;
                            case 1:
                                insertNodeInRunningEdge(migrNode,coalescingEdge);
                                iRequestedPop = dest_pop;
                                break;
                            default:
                                throw "The index of the migration source is out of bounds.";
                        }
                    }else{
                        insertNodeInRunningEdge(migrNode,coalescingEdge);
                        iRequestedPop = dest_pop;
                    }
                }
                pPopList[source_pop].changeChrSampled(-1);
                pPopList[dest_pop].changeChrSampled(1);
                EventPtr eventWrapper = EventPtr(new MigrationEvent(
                Event::PAST_MIGRATION,dTime,
                dest_pop,source_pop));
                migrNode->setEvent(eventWrapper);
                pEventList->insert(currentEventIt,eventWrapper);
            }else if (iEventType==Event::NEW_COAL){ // coalescent event

                if (!bBuildFromEventList){
                    EventPtr eventWrapper;
                    // pick the two, c1, c2
                    int iChr1,iChr2;
                    NodePtr node0,node1;
                    bool found = false;
                    NodePtr chr1Node,chr2Node;

                    while(!found){
                        iChr1 = static_cast<int>(pRandNumGenerator->unifRV() *
                        pCoalescedNodes->size());
                        NodePtrSet::iterator it = pCoalescedNodes->begin();
                        for (int i=1;i<=iChr1;++i) ++it;
                        chr1Node = *it;
                        if (chr1Node->getPopulation()==iCoalescingPop){
                            while(!found){
                                iChr2 = static_cast<int>(pRandNumGenerator->unifRV() *
                                pCoalescedNodes->size());
                                if (iChr1!=iChr2){
                                    it = pCoalescedNodes->begin();
                                    for (int i=1;i<=iChr2;++i) ++it;
                                    chr2Node = *it;
                                    //llf add coalesce condition
                                    //if (chr2Node->getPopulation()==iCoalescingPop) found = true;
                                    if (chr2Node->getPopulation()==iCoalescingPop){
                                      //const char* pGSStr1 = ((GeneString*)chr1Node->getAssoData()).c_str();                                              
                                      //const char* pGSStr2 = ((GeneString*)chr2Node->getAssoData()).c_str();                                              
                                      char cChr1Value = chr1Node->getCData();                                             
                                      char cChr2Value = chr2Node->getCData();                                             
                                      if(cChr1Value == cChr2Value){
                                        found = true;
                                      }else{
                                        if(nValueCnt[0] <=1 || nValueCnt[1] <= 1)
                                        {
                                          found = true;                 
                                        }                                              
                                      }                                             
                                    }
                                }
                            }
                        }
                    }
                    node0 = chr1Node;
                    node1 = chr2Node;
                    NodePtr parentNode = NodePtr(new Node
                    (Node::COAL,node0->getPopulation(),dTime));
                    //llf added
                    char cNode0 = node0->getCData();
                    char cNode1 = node1->getCData();                    
                    char cParent;
                    if(cNode0 == cNode1){
                      cParent = cNode0;         
                    }else{
                        if(nValueCnt[0] == 1){
                           cParent = '1';               
                        }else if(nValueCnt[1] == 1){
                           cParent = '0';                
                        }else{
                           cout<<"exception: cNode1 != cNode1 and no one left 1"<<endl;  
                           cout<<"nValueCnt[0]:"<<nValueCnt[0]<<"  nValueCnt[1]:"<<nValueCnt[1]<<endl;
                           cout<<"cNode0:"<<cNode0<<" cNode1:"<<cNode1<<endl;
                           throw "exception: cNode1 != cNode1 and no one left 1";    
                        }
                    }
                    nValueCnt[cNode0 - '0']--;
                    nValueCnt[cNode1 - '0']--;
                    nValueCnt[cParent - '0']++;                    
                    parentNode->setCData(cParent);
                    //llf added end.
                    EdgePtr edge0 = EdgePtr(new Edge(parentNode,node0));
                    node0->addNewEdge(Node::TOP_EDGE,edge0);
                    parentNode->addNewEdge(Node::BOTTOM_EDGE,edge0);
                    EdgePtr edge1 = EdgePtr(new Edge(parentNode,node1));
                    node1->addNewEdge(Node::TOP_EDGE,edge1);
                    parentNode->addNewEdge(Node::BOTTOM_EDGE,edge1);
                    this->addEdge(edge0);
                    this->addEdge(edge1);
                    pCoalescedNodes->erase(node0);
                    pCoalescedNodes->erase(node1);
                    eventWrapper = EventPtr(new CoalEvent(
                    Event::PAST_COAL,dTime,
                    parentNode->getPopulation()));
                    pCoalescedNodes->insert(parentNode);
                    iChromCounter = pCoalescedNodes->size();
                    if (iChromCounter==1){
                        localMRCA = grandMRCA = parentNode;
                        bDoneBuild = true;
                    }
                    pPopList[iCoalescingPop].changeChrSampled(-1);
                    parentNode->setEvent(eventWrapper);
                    pEventList->insert(currentEventIt,eventWrapper);
                }else{
                    // the case where the proposed time is beyond the last
                    // event time
                    // or the proposed time is in the middle and an eligible
                    // coalescent event is found.
                    newCoalEvent = EventPtr(new CoalEvent(
                    Event::PAST_COAL,dTime,iRequestedPop));
                    pEventList->insert(currentEventIt,newCoalEvent);
                    bDoneBuild = true;
                }
            }else{
                if (pConfig->bDebug) cerr<< "No random event could be assigned.";
            }

        }
        dLastTime = dTime;
    }
    if (!bBuildFromEventList){
        delete pCoalescedNodes;
    }
}

void GraphBuilder::pruneARG(int iHistoryMax){
    bool found = false;
    EdgePtrList candidateEdges;
    EdgePtrList::iterator it = pEdgeListInARG->begin();
    // look for old edges that have an expired graph iteration value
    while (it!=pEdgeListInARG->end()){
        EdgePtr curEdge = *it;
        if (curEdge->iGraphIteration<=iHistoryMax)
            candidateEdges.push_back(curEdge);
        ++it;
    }
    candidateEdges.sort(byBottomNode());
    do{
        EdgePtr oldEdge;
        found = false;
        // sort the edges from top to bottom as the pruning order
        EdgePtrList::iterator it2 = candidateEdges.begin();
        while(!found && it2!=candidateEdges.end()){
            EdgePtr curEdge = *it2;
            if (!curEdge->bDeleted &&curEdge->getBottomNodeRef()->
            getType()==Node::XOVER){
                oldEdge = curEdge;
                found = true;
            }else{
                ++it2;
            }
        }
        if (found){
            NodePtr & xoverNode = oldEdge->getBottomNodeRef();
            EdgePtr edge0 = xoverNode->getTopEdgeByIndex(0);
            EdgePtr edge1 = xoverNode->getTopEdgeByIndex(1);
            EdgePtr edgeToPrune = edge0->iGraphIteration<
            edge1->iGraphIteration?edge0:edge1;

            pruneEdgesAbove(edgeToPrune);

            edge0 = xoverNode->getTopEdgeByIndex(0);
            edge1 = xoverNode->getTopEdgeByIndex(1);

            EdgePtr otherTopEdge = edge0->iGraphIteration>
            edge1->iGraphIteration?edge0:edge1;

            EdgePtr bottomEdge = xoverNode->getBottomEdgeByIndex(0);

            if (grandMRCA->getHeight()>xoverNode->getHeight()){
                mergeEdges(otherTopEdge,bottomEdge);
                xoverNode->removeEvent();
            }else{
                // we just removed a loop above the grandMRCA which
                // includes the edges around the xover point.
            }
        }
    }while(found);
}

void GraphBuilder::addMutations(double startPos,double endPos){
    bool bEndMutate = false;
    while(!bEndMutate){
        // find the next point on this interval
        startPos+=pRandNumGenerator->expRV(dLastTreeLength*
        pConfig->dTheta);
        if (startPos>=endPos){
            bEndMutate = true;
        }else{
            //if (pMutationPtrVector->size()==0){
            //   cout<<HAPLOBEGIN<<endl;
            //}
            double dRandomSpot = pRandNumGenerator->unifRV() * dLastTreeLength;
            double temp=-1.;
            EdgePtr selectedEdge = getRandomEdgeOnTree(temp,dRandomSpot);
            mutateBelowEdge(selectedEdge);
            NodePtrVector::iterator it;

            cout<<MUTATIONSITE<<FIELD_DELIMITER<<pMutationPtrVector->size()<<
            FIELD_DELIMITER<< setw(15) << setprecision(9)<<startPos<<
            FIELD_DELIMITER;
            unsigned int iSampleSize = pConfig->iSampleSize;
            for (unsigned int iSampleIndex=0;iSampleIndex<iSampleSize;++iSampleIndex){
                SampleNode * sample = static_cast<SampleNode*>(pSampleNodeArray[iSampleIndex].get());
                sites[iSampleIndex]=sample->bAffected;
                cout<<sample->bAffected;
                sample->bAffected=false;
            }
            cout<<endl;
            double dFreq=0.;
            if (pConfig->bSNPAscertainment){
                // first compute the MAF
                int counts=0;
                for (unsigned int i=0;i<iSampleSize;++i){
                    counts=counts+sites[i];
                }
                dFreq = 1.*counts/iSampleSize;
                if (pConfig->bFlipAlleles && dFreq>.5){
                    for (unsigned int i=0;i<iSampleSize;++i){
                        sites[i]=!sites[i];
                    }
                    dFreq = 1.-dFreq;
                }
                AlleleFreqBinPtr query(new AlleleFreqBin(dFreq,dFreq,0.));
                AlleleFreqBinPtrSet::iterator it = pConfig->pAlleleFreqBinPtrSet->find(query);
                if (it!=pConfig->pAlleleFreqBinPtrSet->end()){
                    AlleleFreqBinPtr bin = *it;
                    ++bin->iObservedCounts;
                }else throw "Did not find a frequency range for freq";
            }
            pMutationPtrVector->push_back(MutationPtr(new Mutation(startPos,dFreq)));

        }
    }
}

bool GraphBuilder::getNextPos(double & curPos,HotSpotBinPtrList::iterator & hotSpotIt){
    bool bBinCrossed = false;
    if (hotSpotIt==pConfig->pHotSpotBinPtrList->end()){
        curPos+=pRandNumGenerator->expRV(getRate());
    }else{
        HotSpotBinPtr hotspot = *hotSpotIt;
        double startPos = hotspot->dStart;
        double endPos = hotspot->dEnd;
        if (curPos>=startPos&&curPos<endPos){
            curPos+=pRandNumGenerator->expRV(getRate()*hotspot->dRate);
            if (curPos>endPos){
                bBinCrossed = true;
                curPos = endPos;
                ++hotSpotIt;
            }
        }else if (curPos<startPos){
            curPos+=pRandNumGenerator->expRV(getRate());
            if (curPos>startPos){
                bBinCrossed = true;
                curPos = startPos;
            }
        }else{
            cerr<<"startPos is "<<startPos<<" endPos is "
            <<endPos<<" and curPos is "<<curPos<<endl;
            throw "Shouldn't be here for variable recomb";
        }
    }
    return bBinCrossed;
}

bool GraphBuilder::checkPendingGeneConversions(double & curPos){
    bool found = false;
    GeneConversionPtrSet::iterator it = pGeneConversionPtrSet->begin();
    while(!found){
        if (it!=pGeneConversionPtrSet->end()){
            GeneConversionPtr gc= *it;
            if (gc->dEndPos<curPos){
                EdgePtr edge0 = gc->xOverNode->getTopEdgeByIndex(0);
                EdgePtr edge1 = gc->xOverNode->getTopEdgeByIndex(1);
                if (gc->xOverNode->bDeleted ||
                edge1->iGraphIteration!=iGraphIteration){
                    if (pConfig->bDebug){
                        cerr<<"Deleting gene conversion because:"<<endl;
                        if (gc->xOverNode->bDeleted)
                          cerr<<"xover node was deleted\n";
                        else if (edge1->iGraphIteration!=iGraphIteration)
                          cerr<<"new edge "<<edge1->getBottomNodeRef()->getHeight()<<" to "<<
                          edge1->getTopNodeRef()->getHeight()<<" was not ancestral\n";
                    }
                    delete(*it);
                    pGeneConversionPtrSet->erase(it++);
                }else{
                    if (pConfig->bDebug){
                        cerr<<"Closing a gene conversion: "<<
                        "Backtracking position from "<<curPos<<" to "<<
                        gc->dEndPos<<endl;
                    }
                    curPos = gc->dEndPos;
                    this->gcOldEdge = edge0;
                    this->gcNewEdge = edge1;
                    delete(*it);
                    pGeneConversionPtrSet->erase(it++);
                    return true;
                }
            }else{
                found = true;
            }
        }else found = true;
    }
    return false;
}

void GraphBuilder::build(){
    double curPos = 0.0,lastPos = 0.0,dMaxPos = 1.0;
    cerr<<"Debugging: "<<pConfig->bDebug<<endl;
    NodePtr preXoverNode;
    HotSpotBinPtrList::iterator hotSpotIt;
    if (pConfig->bVariableRecomb){
      hotSpotIt=pConfig->pHotSpotBinPtrList->begin();
    }
    
    //llf build MutSolver
    Site& firstSite = pSample->getSite(0);
    double firstSitePos = firstSite.getPos();
    Site& lastSite = pSample->getSite(pSample->getSiteNum()-1);
    double lastSitePos = lastSite.getPos();
    //llf end
    // gene conversion stuff
    GeneConversionPtr newGC;
    double dLogTractRatio = log((pConfig->iGeneConvTract-1.)/pConfig->iGeneConvTract);
    const int XOVER=0,GCSTART=1,GCEND=2;
    int xovers=0,gcstarts=0,gcends=0;
    int lastRE;
    int iHistoryMax = 0;
    do{
        if (iGraphIteration==0){
            cerr<<endl<<"Graph Builder begin"<<endl;
            NodePtr dummy1;
            EventPtr dummy2;
            double current=clock();
            this->traverseEvents(false,dummy1,dummy2);
            //llf
            this->dCurPos = firstSitePos;
            curPos = firstSitePos;
            lastPos = curPos;
            //llf end
            cerr<<"Time for build prior tree: "<<(clock()-current)<<" seconds\n";
        }else{
            // at this point decide whether we invoke a plain x-over
            // or a new gene conversion event
            this->bBeginGeneConversion = false;
            lastRE=XOVER;
            if (this->bEndGeneConversion){
              lastRE=GCEND;
            }else{
              this->bBeginGeneConversion = pRandNumGenerator->unifRV()<
              (pConfig->dGeneConvRatio/(pConfig->dGeneConvRatio+1))?true:false;
              if (bBeginGeneConversion){
                lastRE=GCSTART;
                double dTractLen = (1.+log(pRandNumGenerator->unifRV())/
                dLogTractRatio)/pConfig->dSeqLength;
                if (pConfig->bDebug) cerr<<"Proposing tract length: "
                <<dTractLen<<endl;
                    newGC = GeneConversionPtr(new GeneConversion(
                    curPos+dTractLen));
                    pGeneConversionPtrSet->insert(newGC);
              }
            }
            //cerr<<"before invokeRecombination"<<endl;
            invokeRecombination(newGC,preXoverNode);
            //cerr<<"end invokeRecombination"<<endl;
            switch(lastRE){
            case XOVER:
              ++xovers;
              break;
            case GCSTART:
              ++gcstarts;
              break;
            case GCEND:
              ++gcends;
              break;
            }
            // mark the graph edges as the local tree
            // cerr<<"before markCurrentTree"<<endl;
            markCurrentTree();
            // cerr<<"end markCurrentTree"<<endl;
            //llf added
            iCurrentTreeId = iGraphIteration;
            //llf add end
            if (!bIncrementHistory){
                double dBoundary = curPos - dTrailingGap;
                if (dBoundary>0.){
                  bIncrementHistory = true;
                }
            }else{
                ++iHistoryMax;
            }
            //cerr<<"iHistoryMax: "<<iHistoryMax<<endl;
            if (iHistoryMax>=0){
                pruneARG(iHistoryMax);
            }
        }

        initializeCurrentTree();

        cerr<<"Tree:"<<iGraphIteration<<
        ",pos:"<<curPos<<
        ",len:"<<dLastTreeLength<<
        ",TMRCA:"<<localMRCA->getHeight()<<
        ",ARG:"<<
        ",len:"<<dArgLength<<
        ",TMRCA:"<<grandMRCA->getHeight()<<
        endl;
        if (pConfig->bVariableRecomb){
            bool bBinCrossed;
            do{
                bBinCrossed = getNextPos(curPos,hotSpotIt);
            }while(bBinCrossed);
        }else{
            //llf added, find next pos after checking mutation on samplesite.
            //curPos+=pRandNumGenerator->expRV(getRate());
           /* 
            if(curPos < lastSitePos){
               //cerr<<"before findNextSolveFailPos"<<endl;
               double nextSolveFailPos = pMutSolver->findNextSolveFailPos(localMRCA,*pSample,curPos);
               double PosbeforeNextFail = pMutSolver->PosbeforeNextSolveFail(localMRCA,*pSample,curPos);
               //cerr<<"end findNextSolveFailPos"<<endl;
               double oldCurPos = curPos;
               if(nextSolveFailPos != 1.0){
                   curPos = PosbeforeNextFail + pRandNumGenerator->unifRV() * (nextSolveFailPos - PosbeforeNextFail);
               }else{
                   curPos += pRandNumGenerator->expRV(getRate());
               }
               //printf("Nextpos:%.5lf, oldPos:%.5lf, nextSolveFailPos:%.5lf\n",curPos,oldCurPos,nextSolveFailPos);
            }else{
               curPos += pRandNumGenerator->expRV(getRate());      
            }
            //wy added
            */
            /* expential prior
           if(curPos < lastSitePos){
               //cerr<<"before findNextSolveFailPos"<<endl;
               double nextSolveFailPos = pMutSolver->findNextSolveFailPos(localMRCA,*pSample,curPos);
               //cerr<<"end findNextSolveFailPos"<<endl;
               double distance = pRandNumGenerator->expRV(getRate());
                if(curPos+distance > nextSolveFailPos){
                double PosbeforeNextFail = pMutSolver->PosbeforeNextSolveFail(localMRCA,*pSample,curPos);
                 distance = pRandNumGenerator->unifRV() * (nextSolveFailPos - PosbeforeNextFail);
                 distance = distance + PosbeforeNextFail - curPos;
                }
               double oldCurPos = curPos;
               curPos = curPos + distance;
               printf("Nextpos:%.5lf, oldPos:%.5lf, nextSolveFailPos:%.5lf\n",curPos,oldCurPos,nextSolveFailPos);
            }else{
            curPos += pRandNumGenerator->expRV(getRate());
            }
            this->dCurPos = curPos;
            */
            //no prior
            if(curPos < lastSitePos){
               //cerr<<"before findNextSolveFailPos"<<endl;
               double nextSolveFailPos = pMutSolver->findNextSolveFailPos(localMRCA,*pSample,curPos);
               double PosbeforeNextFail = pMutSolver->PosbeforeNextSolveFail(localMRCA,*pSample,curPos);
               double distance = pRandNumGenerator->unifRV() * (nextSolveFailPos - curPos);
               //distance = distance - curPos;
               //cerr<<"end findNextSolveFailPos"<<endl;
               double oldCurPos = curPos;
               curPos = curPos + distance;
               //printf("Nextpos:%.5lf, oldPos:%.5lf, nextSolveFailPos:%.5lf\n",curPos,oldCurPos,nextSolveFailPos);
            }else{
            curPos = 1.0;
            }
            this->dCurPos = curPos;
            //llf added end
        }
        // check if we reached the end of the region
        if (curPos>dMaxPos) curPos=dMaxPos;
        if (pConfig->bNewickFormat){
            //uint iSegLength = (curPos-lastPos)*pConfig->dSeqLength;
            float iSegLength = (curPos-lastPos)*pConfig->dSeqLength;
            cout<<NEWICKTREE<<"\t["<<iSegLength<<"]"<<
            getNewickTree(localMRCA->getHeight(),localMRCA)<<";"<<endl;
        }
        // check if there was an existing gene conversion event that needs
        // to be closed. backtrack if necessary.
        this->bEndGeneConversion  = checkPendingGeneConversions(curPos);
        if (pConfig->dTheta>0.0){
            addMutations(lastPos,curPos);
        }
        lastPos = curPos;
        ++iGraphIteration;
    }while(curPos<dMaxPos);
    cerr<<"Completed the chromosome at position "<<curPos<<endl;
    //if (pMutationPtrVector->size()>0){
    //        cout<<HAPLOEND<<endl;
    //}
    cerr<<"gcstarts:"<<gcstarts<<" gcends:"<<gcends<<" xovers:"<<xovers<<endl;
}

