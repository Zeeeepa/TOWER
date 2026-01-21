import { GoogleGenAI } from "@google/genai";

let aiClient: GoogleGenAI | null = null;

export const initGemini = () => {
  // Fix: Use VITE_ prefix for environment variables in Vite
  const apiKey = import.meta.env.VITE_GEMINI_API_KEY;
  if (!aiClient && apiKey) {
    aiClient = new GoogleGenAI({ apiKey: apiKey });
  }
};

export const generateSimulationResponse = async (
  prompt: string, 
  systemInstruction: string = "You are a helpful AI assistant."
): Promise<string> => {
  if (!aiClient) {
    initGemini();
    if (!aiClient) return "Error: API Key not found. Please set VITE_GEMINI_API_KEY in .env.local file.";
  }

  try {
    const response = await aiClient!.models.generateContent({
      model: "gemini-3-flash-preview",
      contents: prompt,
      config: {
        systemInstruction: systemInstruction,
      }
    });
    return response.text || "No response text generated.";
  } catch (error) {
    console.error("Gemini API Error:", error);
    return "Error generating response.";
  }
};