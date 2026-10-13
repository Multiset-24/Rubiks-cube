import { useState } from 'react';
import type { Quiz as QuizData } from '../lib/api';

interface Props {
  quiz: QuizData;
  onAnswered: (correct: boolean) => void;
}

// Shown before a step's moves when quizzes are on: the learner has to read the
// cube (which pattern, how many repeats) before seeing the answer.
export function Quiz({ quiz, onAnswered }: Props) {
  const [picked, setPicked] = useState<number | null>(null);
  const answered = picked !== null;
  return (
    <div className="quiz">
      <p className="quiz__label">Quick check</p>
      <p className="quiz__question">{quiz.question}</p>
      <div className="quiz__options">
        {quiz.options.map((o, i) => {
          const state = !answered ? '' : i === quiz.answer ? 'quiz__opt--right' : i === picked ? 'quiz__opt--wrong' : '';
          return (
            <button
              key={i}
              type="button"
              className={`quiz__opt ${state}`}
              disabled={answered}
              onClick={() => setPicked(i)}
            >
              {o}
            </button>
          );
        })}
      </div>
      {answered && (
        <div className="quiz__feedback">
          <p>{picked === quiz.answer ? 'Correct.' : `The answer is “${quiz.options[quiz.answer]}”.`} {quiz.explanation}</p>
          <button type="button" className="btn btn--primary" onClick={() => onAnswered(picked === quiz.answer)}>
            Show me the moves
          </button>
        </div>
      )}
    </div>
  );
}
