 //
 // Created by Saurav on 24-06-2026.
 //

 #ifndef RUBIKS_CUBE_SOLVER_CORNERPATTERNDATABASE_H
 #define RUBIKS_CUBE_SOLVER_CORNERPATTERNDATABASE_H

 #include "../Model/RubiksCube.h"
 #include "../Pattern_Database/PatternDatabase.h"

 #include "PermutationIndexer.h"
 using namespace std;

 class CornerPatternDatabase : public PatternDatabase {

     typedef RubiksCube::FACE F;

     PermutationIndexer<8> permIndexer;

 public:
     CornerPatternDatabase();
     CornerPatternDatabase(uint8_t init_val);
     uint32_t getDatabaseIndex(const RubiksCube& cube) const;

 };


 #endif //RUBIKS_CUBE_SOLVER_CORNERPATTERNDATABASE_H