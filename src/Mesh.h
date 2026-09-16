#ifndef MESH_H
#define MESH_H

#include <glad/glad.h>
#include <vector>
#include <memory>

#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <map>
#include <set>
#include <string>
#include <cmath>
#include <iostream>

class Mesh {
public:
  virtual ~Mesh();

  const std::vector<glm::vec3> &vertexPositions() const { return _vertexPositions; }
  std::vector<glm::vec3> &vertexPositions() { return _vertexPositions; }

  const std::vector<glm::vec3> &vertexNormals() const { return _vertexNormals; }
  std::vector<glm::vec3> &vertexNormals() { return _vertexNormals; }

  const std::vector<glm::vec2> &vertexTexCoords() const { return _vertexTexCoords; }
  std::vector<glm::vec2> &vertexTexCoords() { return _vertexTexCoords; }

  const std::vector<float> &vertexLeafVariations() const { return _vertexLeafVariations; }
  std::vector<float> &vertexLeafVariations() { return _vertexLeafVariations; }

  const std::vector<glm::uvec3> &triangleIndices() const { return _triangleIndices; }
  std::vector<glm::uvec3> &triangleIndices() { return _triangleIndices; }

  /// Compute the parameters of a sphere which bounds the mesh
  void computeBoundingSphere(glm::vec3 &center, float &radius) const;

  void recomputePerVertexNormals(bool angleBased = false);
  void recomputePerVertexTextureCoordinates( );

  void init();
  void initOldGL();
  void render();
  void clear();

  void addPlan(float square_half_side = 1.0f);

 void subdivideLinear() {
  // Save current state for undo
    saveToHistory();

    std::vector<glm::vec3> newVertices = _vertexPositions;
    std::vector<glm::uvec3> newTriangles;

    struct Edge {
      unsigned int a , b;
      Edge( unsigned int c , unsigned int d ) : a( std::min<unsigned int>(c,d) ) , b( std::max<unsigned int>(c,d) ) {}
      bool operator < ( Edge const & o ) const {   return a < o.a  ||  (a == o.a && b < o.b);  }
      bool operator == ( Edge const & o ) const {   return a == o.a  &&  b == o.b;  }
    };
    std::map< Edge , unsigned int > newVertexOnEdge;
    for(unsigned int tIt = 0 ; tIt < _triangleIndices.size() ; ++tIt) {
      unsigned int a = _triangleIndices[tIt][0];
      unsigned int b = _triangleIndices[tIt][1];
      unsigned int c = _triangleIndices[tIt][2];


      Edge Eab(a,b);
      unsigned int oddVertexOnEdgeEab = 0;
      if( newVertexOnEdge.find( Eab ) == newVertexOnEdge.end() ) {
        newVertices.push_back( (_vertexPositions[ a ] + _vertexPositions[ b ]) / 2.f );
        oddVertexOnEdgeEab = newVertices.size() - 1;
        newVertexOnEdge[Eab] = oddVertexOnEdgeEab;
      }
      else { oddVertexOnEdgeEab = newVertexOnEdge[Eab]; }


      Edge Ebc(b,c);
      unsigned int oddVertexOnEdgeEbc = 0;
      if( newVertexOnEdge.find( Ebc ) == newVertexOnEdge.end() ) {
        newVertices.push_back( (_vertexPositions[ b ] + _vertexPositions[ c ]) / 2.f );
        oddVertexOnEdgeEbc = newVertices.size() - 1;
        newVertexOnEdge[Ebc] = oddVertexOnEdgeEbc;
      }
      else { oddVertexOnEdgeEbc = newVertexOnEdge[Ebc]; }

      Edge Eca(c,a);
      unsigned int oddVertexOnEdgeEca = 0;
      if( newVertexOnEdge.find( Eca ) == newVertexOnEdge.end() ) {
        newVertices.push_back( (_vertexPositions[ c ] + _vertexPositions[ a ]) / 2.f );
        oddVertexOnEdgeEca = newVertices.size() - 1;
        newVertexOnEdge[Eca] = oddVertexOnEdgeEca;
      }
      else { oddVertexOnEdgeEca = newVertexOnEdge[Eca]; }

      // set new triangles :
      newTriangles.push_back( glm::uvec3( a , oddVertexOnEdgeEab , oddVertexOnEdgeEca ) );
      newTriangles.push_back( glm::uvec3( oddVertexOnEdgeEab , b , oddVertexOnEdgeEbc ) );
      newTriangles.push_back( glm::uvec3( oddVertexOnEdgeEca , oddVertexOnEdgeEbc , c ) );
      newTriangles.push_back( glm::uvec3( oddVertexOnEdgeEab , oddVertexOnEdgeEbc , oddVertexOnEdgeEca ) );
    }

    // after that:
    _triangleIndices = newTriangles;
    _vertexPositions = newVertices;
    recomputePerVertexNormals( );
    recomputePerVertexTextureCoordinates( );
  }

  void subdivideLoopNew() {
    // Declare new vertices and new triangles. Initialize the new positions for the even vertices with (0,0,0):
    std::vector<glm::vec3> newVertices( _vertexPositions.size() , glm::vec3(0,0,0) );
    std::vector<glm::uvec3> newTriangles;

    struct Edge {
      unsigned int a , b;
      Edge( unsigned int c , unsigned int d ) : a( std::min<unsigned int>(c,d) ) , b( std::max<unsigned int>(c,d) ) {}
      bool operator < ( Edge const & o ) const {   return a < o.a  ||  (a == o.a && b < o.b);  }
      bool operator == ( Edge const & o ) const {   return a == o.a  &&  b == o.b;  }
    };

    std::map< Edge , unsigned int > newVertexOnEdge; // this will be useful to find out whether we already inserted an odd vertex or not
    std::map< Edge , std::set< unsigned int > > trianglesOnEdge; // this will be useful to find out if an edge is boundary or not
    std::vector< std::set< unsigned int > > neighboringVertices( _vertexPositions.size() ); // this will be used to store the adjacent vertices, i.e., neighboringVertices[i] will be the list of vertices that are adjacent to vertex i.
    std::vector< bool > evenVertexIsBoundary( _vertexPositions.size() , false );


    // I) First, compute the valences of the even vertices, the neighboring vertices required to update the position of the even vertices, and the boundaries:
    for(unsigned int tIt = 0 ; tIt < _triangleIndices.size() ; ++tIt) {
      unsigned int a = _triangleIndices[tIt][0];
      unsigned int b = _triangleIndices[tIt][1];
      unsigned int c = _triangleIndices[tIt][2];

 //TODO: Remember the faces shared by the edge

      neighboringVertices[ a ].insert( b );
      neighboringVertices[ a ].insert( c );
      neighboringVertices[ b ].insert( a );
      neighboringVertices[ b ].insert( c );
      neighboringVertices[ c ].insert( a );
      neighboringVertices[ c ].insert( b );
    }

    // The valence of a vertex is the number of adjacent vertices:
    std::vector< unsigned int > evenVertexValence( _vertexPositions.size() , 0 );
    for( unsigned int v = 0 ; v < _vertexPositions.size() ; ++v ) {
      evenVertexValence[ v ] = neighboringVertices[ v ].size();
    }
//TODO: Identify even vertices (clue: check the number of triangles) and remember immediate neighbors for further calculation
    // II) Then, compute the positions for the even vertices: (make sure that you handle the boundaries correctly)
    for(unsigned int v = 0 ; v < _vertexPositions.size() ; ++v) {
//TODO: Compute the coordinates for even vertices - check both the cases - ordinary and extraordinary
    }


    // III) Then, compute the odd vertices:
    for(unsigned int tIt = 0 ; tIt < _triangleIndices.size() ; ++tIt) {
      unsigned int a = _triangleIndices[tIt][0];
      unsigned int b = _triangleIndices[tIt][1];
      unsigned int c = _triangleIndices[tIt][2];


      Edge Eab(a,b);
      unsigned int oddVertexOnEdgeEab = 0;
      if( newVertexOnEdge.find( Eab ) == newVertexOnEdge.end() ) {
        newVertices.push_back( glm::vec3(0,0,0) );
        oddVertexOnEdgeEab = newVertices.size() - 1;
        newVertexOnEdge[Eab] = oddVertexOnEdgeEab;
      }
      else { oddVertexOnEdgeEab = newVertexOnEdge[Eab]; }

//TODO: Update odd vertices


      Edge Ebc(b,c);
      unsigned int oddVertexOnEdgeEbc = 0;
      if( newVertexOnEdge.find( Ebc ) == newVertexOnEdge.end() ) {
        newVertices.push_back( glm::vec3(0,0,0) );
        oddVertexOnEdgeEbc = newVertices.size() - 1;
        newVertexOnEdge[Ebc] = oddVertexOnEdgeEbc;
      }
      else { oddVertexOnEdgeEbc = newVertexOnEdge[Ebc]; }

//TODO: Update odd vertices


      Edge Eca(c,a);
      unsigned int oddVertexOnEdgeEca = 0;
      if( newVertexOnEdge.find( Eca ) == newVertexOnEdge.end() ) {
        newVertices.push_back( glm::vec3(0,0,0) );
        oddVertexOnEdgeEca = newVertices.size() - 1;
        newVertexOnEdge[Eca] = oddVertexOnEdgeEca;
      }
      else { oddVertexOnEdgeEca = newVertexOnEdge[Eca]; }

//TODO: Update odd vertices


      // set new triangles :
      newTriangles.push_back( glm::uvec3( a , oddVertexOnEdgeEab , oddVertexOnEdgeEca ) );
      newTriangles.push_back( glm::uvec3( oddVertexOnEdgeEab , b , oddVertexOnEdgeEbc ) );
      newTriangles.push_back( glm::uvec3( oddVertexOnEdgeEca , oddVertexOnEdgeEbc , c ) );
      newTriangles.push_back( glm::uvec3( oddVertexOnEdgeEab , oddVertexOnEdgeEbc , oddVertexOnEdgeEca ) );
    }


    // after that:
    _triangleIndices = newTriangles;
    _vertexPositions = newVertices;
    recomputePerVertexNormals( );
    recomputePerVertexTextureCoordinates( );
  }





  void subdivideLoop() {
    // ===========================================
    // LOOP SUBDIVISION IMPLEMENTATION
    // ===========================================
    
    // Safety check: prevent subdivision if mesh is too large
    const unsigned int MAX_TRIANGLES = 2000000;
    if(_triangleIndices.size() > MAX_TRIANGLES) {
      std::cout << "[Loop Subdivision] Mesh too large (" << _triangleIndices.size() 
                << " triangles). Skipping to prevent freeze. Max allowed: " << MAX_TRIANGLES << std::endl;
      return;
    }
    
    // Save current state for undo
    saveToHistory();
    
    std::cout << "[Loop Subdivision] Starting... Input: " << _vertexPositions.size() 
              << " vertices, " << _triangleIndices.size() << " triangles" << std::endl;
    
    // I) Declare new vertices and triangles
    std::vector<glm::vec3> newVertices(_vertexPositions.size(), glm::vec3(0,0,0));
    std::vector<glm::uvec3> newTriangles;

    // Edge structure for consistent edge identification
    struct Edge {
      unsigned int a, b;
      Edge(unsigned int c, unsigned int d) : a(std::min<unsigned int>(c,d)), b(std::max<unsigned int>(c,d)) {}
      bool operator < (Edge const & o) const { return a < o.a || (a == o.a && b < o.b); }
      bool operator == (Edge const & o) const { return a == o.a && b == o.b; }
    };

    std::map<Edge, unsigned int> newVertexOnEdge;  // Maps edge to new odd vertex index
    std::map<Edge, std::vector<unsigned int>> trianglesOnEdge;  // Maps edge to adjacent triangles
    std::vector<std::set<unsigned int>> neighboringVertices(_vertexPositions.size());  // Adjacency list

    // II) First pass: Build adjacency information and edge-triangle relationships
    for(unsigned int tIt = 0; tIt < _triangleIndices.size(); ++tIt) {
      unsigned int a = _triangleIndices[tIt][0];
      unsigned int b = _triangleIndices[tIt][1];
      unsigned int c = _triangleIndices[tIt][2];

      // Record neighboring vertices
      neighboringVertices[a].insert(b);
      neighboringVertices[a].insert(c);
      neighboringVertices[b].insert(a);
      neighboringVertices[b].insert(c);
      neighboringVertices[c].insert(a);
      neighboringVertices[c].insert(b);

      // Record triangles on each edge (for boundary detection)
      trianglesOnEdge[Edge(a,b)].push_back(tIt);
      trianglesOnEdge[Edge(b,c)].push_back(tIt);
      trianglesOnEdge[Edge(c,a)].push_back(tIt);
    }

    // Identify boundary vertices and their boundary neighbors
    std::vector<bool> isBoundaryVertex(_vertexPositions.size(), false);
    std::map<unsigned int, std::vector<unsigned int>> boundaryNeighbors;
    
    for(auto& edgePair : trianglesOnEdge) {
      if(edgePair.second.size() == 1) {  // Boundary edge: only one adjacent triangle
        unsigned int v1 = edgePair.first.a;
        unsigned int v2 = edgePair.first.b;
        isBoundaryVertex[v1] = true;
        isBoundaryVertex[v2] = true;
        boundaryNeighbors[v1].push_back(v2);
        boundaryNeighbors[v2].push_back(v1);
      }
    }

    // III) Compute new positions for EVEN vertices (original vertices)
    for(unsigned int v = 0; v < _vertexPositions.size(); ++v) {
      unsigned int n = neighboringVertices[v].size();  // valence
      
      if(isBoundaryVertex[v]) {
        // Boundary vertex: use boundary mask (1/8, 3/4, 1/8)
        glm::vec3 neighborSum(0,0,0);
        for(unsigned int neighbor : boundaryNeighbors[v]) {
          neighborSum += _vertexPositions[neighbor];
        }
        if(boundaryNeighbors[v].size() == 2) {
          newVertices[v] = 0.75f * _vertexPositions[v] + 0.125f * neighborSum;
        } else {
          // Corner vertex - keep position
          newVertices[v] = _vertexPositions[v];
        }
      } else {
        // Interior vertex: use Loop's formula
        // alpha_n = (1/n) * (5/8 - (3/8 + cos(2*pi/n)/4)^2)
        float pi = glm::pi<float>();
        float cosAngle = std::cos(2.0f * pi / float(n));
        float tmp = 0.375f + 0.25f * cosAngle;  // 3/8 + cos(2*pi/n)/4
        float alpha_n = (1.0f / float(n)) * (0.625f - tmp * tmp);  // (1/n) * (5/8 - tmp^2)
        float beta = float(n) * alpha_n;
        
        glm::vec3 neighborSum(0,0,0);
        for(unsigned int neighbor : neighboringVertices[v]) {
          neighborSum += _vertexPositions[neighbor];
        }
        
        newVertices[v] = (1.0f - beta) * _vertexPositions[v] + alpha_n * neighborSum;
      }
    }

    // IV) Process triangles: create ODD vertices and new triangles
    for(unsigned int tIt = 0; tIt < _triangleIndices.size(); ++tIt) {
      unsigned int a = _triangleIndices[tIt][0];
      unsigned int b = _triangleIndices[tIt][1];
      unsigned int c = _triangleIndices[tIt][2];

      // Lambda to compute odd vertex position
      auto computeOddVertex = [&](unsigned int v1, unsigned int v2, unsigned int vOpposite) -> glm::vec3 {
        Edge e(v1, v2);
        if(trianglesOnEdge[e].size() == 1) {
          // Boundary edge: simple midpoint
          return 0.5f * (_vertexPositions[v1] + _vertexPositions[v2]);
        } else {
          // Interior edge: 3/8*(v1+v2) + 1/8*(opposite vertices)
          unsigned int otherTriIdx = (trianglesOnEdge[e][0] == tIt) ? trianglesOnEdge[e][1] : trianglesOnEdge[e][0];
          glm::uvec3 otherTri = _triangleIndices[otherTriIdx];
          
          // Find the opposite vertex in the other triangle
          unsigned int vOtherOpposite = 0;
          for(int i = 0; i < 3; ++i) {
            if(otherTri[i] != v1 && otherTri[i] != v2) {
              vOtherOpposite = otherTri[i];
              break;
            }
          }
          
          return 0.375f * (_vertexPositions[v1] + _vertexPositions[v2]) + 
                 0.125f * (_vertexPositions[vOpposite] + _vertexPositions[vOtherOpposite]);
        }
      };

      // Edge ab
      Edge Eab(a, b);
      unsigned int oddVertexOnEdgeEab = 0;
      if(newVertexOnEdge.find(Eab) == newVertexOnEdge.end()) {
        newVertices.push_back(computeOddVertex(a, b, c));
        oddVertexOnEdgeEab = newVertices.size() - 1;
        newVertexOnEdge[Eab] = oddVertexOnEdgeEab;
      } else {
        oddVertexOnEdgeEab = newVertexOnEdge[Eab];
      }

      // Edge bc
      Edge Ebc(b, c);
      unsigned int oddVertexOnEdgeEbc = 0;
      if(newVertexOnEdge.find(Ebc) == newVertexOnEdge.end()) {
        newVertices.push_back(computeOddVertex(b, c, a));
        oddVertexOnEdgeEbc = newVertices.size() - 1;
        newVertexOnEdge[Ebc] = oddVertexOnEdgeEbc;
      } else {
        oddVertexOnEdgeEbc = newVertexOnEdge[Ebc];
      }

      // Edge ca
      Edge Eca(c, a);
      unsigned int oddVertexOnEdgeEca = 0;
      if(newVertexOnEdge.find(Eca) == newVertexOnEdge.end()) {
        newVertices.push_back(computeOddVertex(c, a, b));
        oddVertexOnEdgeEca = newVertices.size() - 1;
        newVertexOnEdge[Eca] = oddVertexOnEdgeEca;
      } else {
        oddVertexOnEdgeEca = newVertexOnEdge[Eca];
      }

      // Create 4 new triangles per original triangle
      newTriangles.push_back(glm::uvec3(a, oddVertexOnEdgeEab, oddVertexOnEdgeEca));
      newTriangles.push_back(glm::uvec3(oddVertexOnEdgeEab, b, oddVertexOnEdgeEbc));
      newTriangles.push_back(glm::uvec3(oddVertexOnEdgeEca, oddVertexOnEdgeEbc, c));
      newTriangles.push_back(glm::uvec3(oddVertexOnEdgeEab, oddVertexOnEdgeEbc, oddVertexOnEdgeEca));
    }

    // V) Update mesh data
    _triangleIndices = newTriangles;
    _vertexPositions = newVertices;
    recomputePerVertexNormals();
    recomputePerVertexTextureCoordinates();

    // TODO: Implement here the Loop subdivision instead of the straightforward Linear Subdivision.
    // You can have a look at the Linear Subdivision function to take some inspiration from it.
    //
    // A few recommendations / advices (note that the following regards a simple implementation that does not handle boundaries, you can adapt it if you want to handle those):
    // I) start by declaring a vector of new positions "newVertices" and a vector of new triangles "newTriangles".
    //    Do not mix the new quantities and the old ones.
    //    At the end, replace _vertexPositions by newVertices and _triangleIndices by newTriangles, just as it is done in subdivideLinear().
    //    This will help you writing clean code.
    //    Remember: In the Loop subdivision scheme, a new position (in the output mesh at level k+1) is a linear combination of the old vertices positions (at level k).
    //    So, you should NEVER (!!!!!) have in your code something like: newVertices[ v ] += newVertices[ v_neighbor ] * weight;
    // II) Compute the neighbors of all the even vertices. You can use a structure such as "std::vector< std::set< unsigned int > > vertex_neighbors" for example.
    //    This will give you the valence n of a given even vertex v, and the value of the coefficient alpha_n that you need to use in the computation of the new position for v.
    // III) Compute the new positions for the even vertices. If you compute the even vertices first, you will not be tempted to consider the odd vertices as their neighbors (that would be a -- very common, mistake).
    // IV) Process all triangles, insert the odd vertices, compute their position using the subdivision mask, and create four new triangles per old triangle.
    //    You can get inspiration from subdivideLinear() for that part.
    //
    // Good luck! Do not hesitate asking questions, we are here to help you.
  std::cout << "[Loop Subdivision] Done! Output: " << _vertexPositions.size() 
              << " vertices, " << _triangleIndices.size() << " triangles" << std::endl;
  }

  private:
  std::vector<glm::vec3> _vertexPositions;
  std::vector<glm::vec3> _vertexNormals;
  std::vector<glm::vec2> _vertexTexCoords;
  std::vector<float> _vertexLeafVariations;
  std::vector<glm::uvec3> _triangleIndices;

  // Undo history
  std::vector<std::vector<glm::vec3>> _historyPositions;
  std::vector<std::vector<glm::uvec3>> _historyTriangles;
  static const unsigned int MAX_HISTORY = 10;

  GLuint _vao = 0;
  GLuint _posVbo = 0;
  GLuint _normalVbo = 0;
  GLuint _texCoordVbo = 0;
  GLuint _leafVariationVbo = 0;
  GLuint _ibo = 0;

public:
  // Save current state to history (call before subdivision)
  void saveToHistory() {
    if(_historyPositions.size() >= MAX_HISTORY) {
      _historyPositions.erase(_historyPositions.begin());
      _historyTriangles.erase(_historyTriangles.begin());
    }
    _historyPositions.push_back(_vertexPositions);
    _historyTriangles.push_back(_triangleIndices);
  }

  // Undo: restore previous state
  bool undo() {
    if(_historyPositions.empty()) {
      std::cout << "[Undo] No history available!" << std::endl;
      return false;
    }
    
    _vertexPositions = _historyPositions.back();
    _triangleIndices = _historyTriangles.back();
    _historyPositions.pop_back();
    _historyTriangles.pop_back();
    
    recomputePerVertexNormals();
    recomputePerVertexTextureCoordinates();
    
    std::cout << "[Undo] Restored: " << _vertexPositions.size() 
              << " vertices, " << _triangleIndices.size() << " triangles" << std::endl;
    return true;
  }

  // Check if undo is available
  bool canUndo() const { return !_historyPositions.empty(); }
  
  // Get history size
  unsigned int historySize() const { return _historyPositions.size(); }
};

// utility: loader
void loadOFF(const std::string &filename, std::shared_ptr<Mesh> meshPtr);

#endif  // MESH_H
