 //
 // Created by Saurav on 24-06-2025.
 //

 #ifndef RUBIKS_CUBE_SOLVER_PATTERNDATABASE_H
 #define RUBIKS_CUBE_SOLVER_PATTERNDATABASE_H

 #include "../Model/RubiksCube.h"
 #include "NibbleArray.h"
 #include "bits/stdc++.h"

 using namespace std;

 class PatternDatabase {
 public:



     NibbleArray database;
     size_t size;
     size_t numItems;
     PatternDatabase();
     PatternDatabase(const size_t size);
     //    Testing for init_val
     PatternDatabase(const size_t size, uint8_t init_val);

     virtual uint32_t getDatabaseIndex(const RubiksCube &cube) const = 0;

      bool setNumMoves(const RubiksCube &cube, const uint8_t numMoves);

      bool setNumMoves(const uint32_t ind, const uint8_t numMoves);

      uint8_t getNumMoves(const RubiksCube &cube) const;

      uint8_t getNumMoves(const uint32_t ind) const;

      size_t getSize() const;

      size_t getNumItems() const;

      bool isFull() const;

      void toFile(const string &filePath) const;

      bool fromFile(const string &filePath);

      vector<uint8_t> inflate() const;

      void reset();
 };


 #endif //RUBIKS_CUBE_SOLVER_PATTERNDATABASE_H