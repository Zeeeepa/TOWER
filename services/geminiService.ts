import { GoogleGenAI } from "@google/genai";

let aiClient: GoogleGenAI | null = null;

export const initGemini = () => {
  if (!aiClient && process.env.API_KEY) {
    aiClient = new GoogleGenAI({ apiKey: process.env.API_KEY });
  }
};

export const generateSimulationResponse = async (
  prompt: string, 
  systemInstruction: string = "You are a helpful AI assistant."
): Promise<string> => {
  if (!aiClient) {
    initGemini();
    if (!aiClient) return "Error: API Key not found. Please set the API_KEY environment variable.";
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